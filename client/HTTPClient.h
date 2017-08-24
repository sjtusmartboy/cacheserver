#include <boost/predef.h> // Tools to identify the OS.

// We need this to enable cancelling of I/O operations on
// Windows XP, Windows Server 2003 and earlier.
// Refer to "http://www.boost.org/doc/libs/1_58_0/
// doc/html/boost_asio/reference/basic_stream_socket/
// cancel/overload1.html" for details.
#ifdef BOOST_OS_WINDOWS
#define _WIN32_WINNT 0x0501

#if _WIN32_WINNT <= 0x0502 // Windows Server 2003 or earlier.
#define BOOST_ASIO_DISABLE_IOCP
#define BOOST_ASIO_ENABLE_CANCELIO	
#endif
#endif

#include <boost/asio.hpp>

#include <thread>
#include <mutex>
#include <memory>
#include <iostream>
#include <fstream>

using namespace boost;

namespace http_errors {
	enum http_error_codes
	{
		invalid_response = 1
	};
	
	class http_errors_category
		: public boost::system::error_category
	{
	public:
		const char* name() const BOOST_SYSTEM_NOEXCEPT { return "http_errors"; }

		std::string message(int e) const {
			switch (e) {
			case invalid_response:
				return "Server response cannot be parsed.";
				break;
			default:
				return "Unknown error.";
				break;
			}
		}
	};
	
	const boost::system::error_category&
		get_http_errors_category()
	{
			static http_errors_category cat;
			return cat;
		}

	boost::system::error_code
		make_error_code(http_error_codes e)
	{
			return boost::system::error_code(
				static_cast<int>(e), get_http_errors_category());
		}
} // namespace http_errors

namespace boost {
	namespace system {
		template<>
		struct is_error_code_enum
			<http_errors::http_error_codes>
		{
			BOOST_STATIC_CONSTANT(bool, value = true);
		};
	} // namespace system
} // namespace boost

class HTTPClient;
class HTTPRequest;
class HTTPResponse;

typedef void(*Callback) (const HTTPRequest& request,
	const HTTPResponse& response,
	const system::error_code& ec);


class HTTPResponse {
	friend class HTTPRequest;
	//The m_response_buf object is used as a stream buffer 
    //for the output stream m_response_stream.
    //usage:
    //boost::asio::streambuf b;
	//std::ostream os(&b);
	//os << "Hello, World!\n";
	HTTPResponse() :
		//m_response_buf(7000),
		m_response_stream(&m_response_buf) //associate stream buffer to stream
	{}
public:

	unsigned int get_status_code() const {
		return m_status_code;
	}

	const std::string& get_status_message() const {
		return m_status_message;
	}

	const std::map<std::string, std::string>& get_headers() {
		return m_headers;
	}

	const std::istream& get_response() const {
		return m_response_stream;
	}

private:
	asio::streambuf& get_response_buf() {
		return m_response_buf;
	}

	void set_status_code(unsigned int status_code) {
		m_status_code = status_code;
	}

	void set_status_message(const std::string& status_message) {
		m_status_message = status_message;
	}

	void add_header(const std::string& name,
		const std::string& value)
	{
		m_headers[name] = value;
	}

private:
	unsigned int m_status_code; // HTTP status code.
	std::string m_status_message; // HTTP status message.

	// Response headers.
	std::map<std::string, std::string> m_headers;
	asio::streambuf m_response_buf;
	std::istream m_response_stream;
};

class HTTPRequest {
	friend class HTTPClient;

	static const unsigned int DEFAULT_PORT = 80;

	HTTPRequest(asio::io_service& ios, unsigned int id) :
		m_port(DEFAULT_PORT),
		m_id(id),
		m_callback(nullptr),
		m_sock(ios),
		m_resolver(ios),
		m_was_cancelled(false),
		m_ios(ios)
	{}
	
public:
	void set_host(const std::string& host);

	void set_port(unsigned int port);

	void set_uri(const std::string& uri);

	void set_callback(Callback callback);

	std::string get_host() const;

	unsigned int get_port() const;

	const std::string& get_uri() const;
	
	const std::string& get_filename() const;

	unsigned int get_id() const ;

	void execute();

	void cancel();
	
private:
	void on_host_name_resolved( 
		const boost::system::error_code& ec, 
		asio::ip::tcp::resolver::iterator iterator);

	
	void on_connection_established(
		const boost::system::error_code& ec,
		asio::ip::tcp::resolver::iterator iterator);
	
	void on_request_sent(const boost::system::error_code& ec,
		std::size_t bytes_transferred);
	
	void on_status_line_received(
		const boost::system::error_code& ec,
		std::size_t bytes_transferred);
	
	
	void on_headers_received(
		const boost::system::error_code& ec, 
		std::size_t bytes_transferred);
	
	void on_response_body_received(
		const boost::system::error_code& ec,
		std::size_t bytes_transferred);
	
	void on_finish(const boost::system::error_code& ec);
		
public:
	std::string filename;

private:
	// Request paramters. 
	std::string m_host;
	unsigned int m_port;
	std::string m_uri;

	// Object unique identifier. 
	unsigned int m_id;

	// Callback to be called when request completes. 
	Callback m_callback;

	// Buffer containing the request line.
	std::string m_request_buf;

	asio::ip::tcp::socket m_sock;
	asio::ip::tcp::resolver m_resolver;

	HTTPResponse m_response;

	bool m_was_cancelled;
	std::mutex m_cancel_mux;

	asio::io_service& m_ios;
};

class HTTPClient {
public:
	HTTPClient(){
		m_work.reset(new boost::asio::io_service::work(m_ios));

		m_thread.reset(new std::thread([this](){
			m_ios.run();
		}));
	}

	std::shared_ptr<HTTPRequest> create_request(unsigned int id)
	{
		return std::shared_ptr<HTTPRequest>(new HTTPRequest(m_ios, id));
	}

	void close() {
		// Destroy the work object. 
		m_work.reset(NULL);

		// Waiting for the I/O thread to exit.
		m_thread->join();
	}

private:
	asio::io_service m_ios;
	std::unique_ptr<boost::asio::io_service::work> m_work;
	std::unique_ptr<std::thread> m_thread;
};

void handler(const HTTPRequest& request,
             const HTTPResponse& response,
             const system::error_code& ec)
{
	if (ec == 0) {
		/*std::cout << "Request #" << request.get_id()
			<< " has completed. Response: "
			//response.get_response() returns the object of type HTTPResponse::m_response_stream
            //which is of type std::istream
            //rdbuf() function returns the associated stream buffer
			<< response.get_response().rdbuf(); */
			std::string filepath = std::string(".") + request.get_filename();
			std::cout <<filepath<<std::endl;
			std::ofstream ofs(filepath, std::ofstream::out);
			ofs << response.get_response().rdbuf(); 
			ofs.close();
			
			//std::cout<<request.get_filename();
			
	}
	else if (ec == asio::error::operation_aborted) {
		std::cout << "Request #" << request.get_id()
			<< " has been cancelled by the user."
			<< std::endl;
	}
	else {
		std::cout << "Request #" << request.get_id()
			<< " failed! Error code = " << ec.value()
			<< ". Error message = " << ec.message()
			<< std::endl;
	}

	return;
}

