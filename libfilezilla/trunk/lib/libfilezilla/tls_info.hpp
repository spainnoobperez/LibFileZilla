#ifndef LIBFILEZILLA_TLS_INFO_HEADER
#define LIBFILEZILLA_TLS_INFO_HEADER

/** \file
 * \brief Classes to query parameters of a TLS session, including the certificate chain
 */

#include "time.hpp"

namespace fz {
/**
 * Represents all relevant information of a X.509 certificate as used by TLS.
 */
class x509_certificate final
{
public:
	class subject_name final
	{
	public:
		std::string name;
		bool is_dns{};
	};

	x509_certificate() = default;
	~x509_certificate() noexcept = default;
	x509_certificate(x509_certificate const&) = default;
	x509_certificate(x509_certificate&&) noexcept = default;
	x509_certificate& operator=(x509_certificate const&) = default;
	x509_certificate& operator=(x509_certificate&&) noexcept = default;

	x509_certificate(
		std::vector<uint8_t> const& rawData,
		fz::datetime const& activation_time, fz::datetime const& expiration_time,
		std::string const& serial,
		std::string const& pkalgoname, unsigned int bits,
		std::string const& signalgoname,
		std::string const& fingerprint_sha256,
		std::string const& fingerprint_sha1,
		std::string const& issuer,
		std::string const& subject,
	    std::vector<subject_name> const& alt_subject_names);

	x509_certificate(
		std::vector<uint8_t> && rawdata,
		fz::datetime const& activation_time, fz::datetime const& expiration_time,
		std::string const& serial,
		std::string const& pkalgoname, unsigned int bits,
		std::string const& signalgoname,
		std::string const& fingerprint_sha256,
		std::string const& fingerprint_sha1,
		std::string const& issuer,
		std::string const& subject,
	    std::vector<subject_name> && alt_subject_names);


	/// The raw, DER-encoded X.509 certificate
	std::vector<uint8_t> get_raw_data() const { return raw_cert_; }

	fz::datetime const& get_activation_time() const { return activation_time_; }
	fz::datetime const& get_expiration_time() const { return expiration_time_; }

	std::string const& get_serial() const { return serial_; }

	/// The public key algorithm used by the certificate
	std::string const& get_pubkey_algorithm() const { return pkalgoname_; }

	/// The number of bits of the public key algorithm
	unsigned int get_pubkey_bits() const { return pkalgobits_; }

	/// The algorithm used for signing, typically the public key algorithm combined with a hash
	std::string const& get_signature_algorithm() const { return signalgoname_; }

	/// Gets fingerprint as hex-encoded sha256
	std::string const& get_fingerprint_sha256() const { return fingerprint_sha256_; }

	/// Gets fingerprint as hex-encoded sha1
	std::string const& get_fingerprint_sha1() const { return fingerprint_sha1_; }

	/** \brief Gets the subject of the certificate as RDN as described in RFC4514
	 *
	 * Never use the CN field to compare it against a hostname, that's what the SANs are for.
	 */
	std::string const& get_subject() const { return subject_; }

	/// Gets the issuer of the certificate as RDN as described in RFC4514
	std::string const& get_issuer() const { return issuer_; }

	/// Gets the alternative subject names (SANSs) of the certificated, usually hostnames
	std::vector<subject_name> const& get_alt_subject_names() const { return alt_subject_names_; }

	explicit operator bool() const { return !raw_cert_.empty(); }

private:
	fz::datetime activation_time_;
	fz::datetime expiration_time_;

	std::vector<uint8_t> raw_cert_;

	std::string serial_;
	std::string pkalgoname_;
	unsigned int pkalgobits_{};

	std::string signalgoname_;

	std::string fingerprint_sha256_;
	std::string fingerprint_sha1_;

	std::string issuer_;
	std::string subject_;

	std::vector<subject_name> alt_subject_names_;
};

/// Information about a TLS session
class tls_session_info final
{
public:
	tls_session_info() = default;
	~tls_session_info() = default;
	tls_session_info(tls_session_info const&) = default;
	tls_session_info(tls_session_info&&) noexcept = default;
	tls_session_info& operator=(tls_session_info const&) = default;
	tls_session_info& operator=(tls_session_info&&) noexcept = default;

	tls_session_info(std::string const& host, unsigned int port,
		std::string const& protocol,
		std::string const& key_exchange,
		std::string const& session_cipher,
		std::string const& session_mac,
		int algorithm_warnings,
		std::vector<x509_certificate>&& certificates,
		bool system_trust,
		bool hostname_mismatch);

	/// The server's hostname used to connect
	std::string const& get_host() const { return host_; }

	/// The server's port
	unsigned int get_port() const { return port_; }

	/// The symmetric algorithm used to encrypt all exchanged application data
	std::string const& get_session_cipher() const { return session_cipher_; }

	/// The MAC used for integrity-protect and authenticate the exchanged application data
	std::string const& get_session_mac() const { return session_mac_; }

	/** \brief The server's certificate chain
	 *
	 * The chain is ordered from the server's own certificate at index 0 up to the self-signed
	 * root CA.
	 *
	 * Chain may be partial.
	 */
	std::vector<fz::x509_certificate> const& get_certificates() const { return certificates_; }

	/// TLS version
	std::string const& get_protocol() const { return protocol_; }

	/// Key exchange algorithm
	std::string const& get_key_exchange() const { return key_exchange_; }

	enum algorithm_warnings_t
	{
		tlsver = 1,
		cipher = 2,
		mac = 4,
		kex = 8
	};

	/// Warnings about old algorithms used, which are considered weak
	int get_algorithm_warnings() const { return algorithm_warnings_; }

	/// Returns true if the server certificate is to be trusted according to
	/// the operating system's trust store.
	bool system_trust() const { return system_trust_; }

	/// True if the hostname in the SANs does not match the requested hostname
	bool mismatched_hostname() const { return hostname_mismatch_; }

private:
	std::string host_;
	unsigned int port_{};

	std::string protocol_;
	std::string key_exchange_;
	std::string session_cipher_;
	std::string session_mac_;
	int algorithm_warnings_{};

	std::vector<x509_certificate> certificates_;

	bool system_trust_{};
	bool hostname_mismatch_{};
};
}

#endif
