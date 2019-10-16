#ifndef LIBFILEZILLA_RATE_LIMITER_HEADER
#define LIBFILEZILLA_RATE_LIMITER_HEADER

/** \file
 * \brief Classes for rate-limiting
 *
 * Rate-limiting is done using token buckets with hierachical limits.
 * Rate is distributed fairly between buckets, with any overflow
 * distributed between buckets still having capacity.
 */

#include "event_handler.hpp"

#include <atomic>
#include <vector>

namespace fz {

namespace rate {
using type = uint64_t;
enum : type {
	unlimited = static_cast<type>(-1)
};
}

namespace direction {
enum type : size_t {
	inbound,
	outbound
};
}

class rate_limiter;

/**
 * \brief Context for rate_limiters
 *
 * Each active \sa rate_limiter must be added to a rate_limit_manager.
 * The rate_limit_manager must exist longer than the rate_limiters.
 *
 * This class implements the timer that periodically adds tokens to buckets.
 * This timer is started and stopped automatically, it does not run when
 * there is no activity to avoid unneeded CPU wakeups.
 */
class FZ_PUBLIC_SYMBOL rate_limit_manager final : public event_handler
{
public:
	explicit rate_limit_manager(event_loop & loop);
	virtual ~rate_limit_manager();

	/**
	 * \brief Adds a limiter to the manager.
	 *
	 * Gets removed automatically when the limiter is destroyed, or manually
	 * when the limiter's \c remove_bucket is called.
	 */
	void add(rate_limiter* limiter);

private:
	friend class rate_limiter;
	friend class bucket_base;
	friend class bucket;

	void record_activity();

	void operator()(event_base const& ev);
	void on_timer(timer_id const&);

	void process(rate_limiter* limiter, bool locked);

	mutex mtx_{false};
	std::vector<rate_limiter*> limiters_;

	std::atomic<timer_id> timer_{};

	std::atomic<int> activity_{2};
};

/// Base class for buckets
class FZ_PUBLIC_SYMBOL bucket_base
{
public:
	virtual ~bucket_base() noexcept = default;

	/**
	 * Removes the bucket from its parent
	 *
	 * /// \warning You _MUST_ call this function in the destructor of the most derived class
	 */
	void remove_bucket();

protected:
	friend class rate_limiter;

	/**
	 * \brief Recursively locks the mutexes of self and all children
	 *
	 * Aggregate buckets need to implement the child traversal.
	 */
	virtual void lock_tree() { mtx_.lock(); }

	/**
	 * \brief Updates weight and usage statistics
	 *
	 * Must only be called with a locked tree
	 */
	virtual void update_stats(bool & active) = 0;

	/**
	 * \brief Returns the weight of the tree
	 *
	 * Must only be called with a locked mutex
	 */
	virtual size_t weight() const { return 1; }

	/**
	 * \brief Returns the number of buckets not yet full
	 *
	 * Must only be called with a locked mutex
	 */
	virtual size_t unsaturated(direction::type const /*d*/) const { return 0; }

	/**
	 * \brief Recursively sets the manager
	 *
	 * Must only be called with a locked tree
	 */
	virtual void set_mgr_recursive(rate_limit_manager * mgr);

	/**
	 * \brief Recursively adds tokens
	 *
	 * Arguments are normalized for a single bucket. For total added, multiply by weight.
	 *
	 * Must only be called with a locked tree
	 *
	 * \returns the overflow, the tokens that could not be distributed.
	 */
	virtual rate::type add_tokens(direction::type const /*d*/, rate::type /*tokens*/, rate::type /*limit*/) = 0;

	/**
	 * \brief Recursively distributes overflow
	 *
	 * Arguments are normalized for a single bucket. For total added, multiply by weight.
	 *
	 * Must only be called with a locked tree
	 *
	 * \returns the overflow, the tokens that could not be distributed.
	 */
	virtual rate::type distribute_overflow(direction::type const /*d*/, rate::type /*tokens*/) { return 0; }

	/**
	 * \brief Recursively unlocks the mutexes of self and all childen.
	 */
	virtual void unlock_tree() { mtx_.unlock(); }

	mutex mtx_{false};
	rate_limit_manager * mgr_{};
	void * parent_{};
	size_t idx_{static_cast<size_t>(-1)};
};

/**
 * \brief A limiter for the attached buckets
 *
 * Distributes tokens fairly between buckets, with overflow distributed
 * so that the total limit is not exceeded.
 *
 * Limiters can either be added to rate_limit_manager, or as sub-limiter
 * to another limiter.
 * For leaf buckets, the actual rate limit is the lowest limit imposed by any of its parents.
 */
class FZ_PUBLIC_SYMBOL rate_limiter final : public bucket_base
{
public:
	rate_limiter() = default;
	explicit rate_limiter(rate_limit_manager * mgr);
	virtual ~rate_limiter();

	/**
	 * \brief Adds a bucket to the limiter.
	 *
	 * Child buckets get removed automatically when they are destroyed, or manually
	 * when the child's \c remove_bucket is called.
	 */
	void add(bucket_base* bucket);

	/**
	 * \brief Sets the number of octets all buckets combined may consume each second.
	 *
	 * Pass \c rate::unlimited if there should be no limit.
	 *
	 * The default limit is \c rate::unlimited.
	 */
	void set_limits(rate::type download_limit, rate::type upload_limit);

	/// Returns current limit
	rate::type limit(direction::type const d);

private:
	friend class bucket_base;
	friend class rate_limit_manager;

	virtual void lock_tree() override;

	bool do_set_limit(direction::type const d, rate::type limit);

	virtual void update_stats(bool & active) override;
	virtual size_t weight() const override { return weight_; }
	virtual size_t unsaturated(direction::type const d) const override { return unused_capacity_[d] ? unsaturated_[d] : 0; }
	virtual void set_mgr_recursive(rate_limit_manager * mgr) override;

	virtual rate::type add_tokens(direction::type const d, rate::type tokens, rate::type limit) override;
	virtual rate::type distribute_overflow(direction::type const d, rate::type tokens) override;

	virtual void unlock_tree() override;

	void pay_debt(direction::type const d);

	rate::type limit_[2] = {rate::unlimited, rate::unlimited};

	std::vector<bucket_base*> buckets_;
	size_t weight_{};

	size_t unsaturated_[2]{};
	std::vector<size_t> scratch_buffer_;
	rate::type overflow_[2]{};
	rate::type merged_tokens_[2]{};
	rate::type debt_[2]{};
	rate::type unused_capacity_[2]{};
	rate::type carry_[2]{};
};

/**
 * \brief A rate-limited token bucket
 */
class FZ_PUBLIC_SYMBOL bucket : public bucket_base
{
public:
	virtual ~bucket();

	/**
	 * \brief Returns available octets
	 *
	 * If this functions returns 0, the caller should wait until after bucket::wakeup got called.
	 */
	rate::type available(direction::type const d);

	/**
	 * \brief Consumes octets
	 *
	 * Only call with a non-zero amount that's less or equal to the number of
	 * available octets. Do not call if an unlimited amount of octets is available.
	 */
	void consume(direction::type const d, rate::type amount);

protected:
	/**
	 * \brief Called in response to unlock_tree if tokens have become available
	 *
	 * Override in derived classes to signal token availability to consumers.
	 */
	virtual void wakeup(direction::type /*d*/) {}

private:
	virtual void update_stats(bool & active) override;
	virtual size_t unsaturated(direction::type const d) const override { return unsaturated_[d] ? 1 : 0; }

	virtual rate::type add_tokens(direction::type const d, rate::type tokens, rate::type limit) override;
	virtual rate::type distribute_overflow(direction::type const d, rate::type tokens) override;

	virtual void unlock_tree() override;

	rate::type available_[2] = {rate::unlimited, rate::unlimited};
	rate::type overflow_multiplier_[2]{1, 1};
	rate::type bucket_size_[2]{rate::unlimited, rate::unlimited};
	bool waiting_[2]{};
	bool unsaturated_[2]{};
};

}

#endif
