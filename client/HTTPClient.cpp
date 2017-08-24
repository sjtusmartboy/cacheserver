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

#include "HTTPClient.h"

void HTTPRequest::set_host(const std::string& host) {
m_host = host;
}

void HTTPRequest::set_port(unsigned int port) {
	m_port = port;
}

void HTTPRequest::set_uri(const std::string& uri) {
	m_uri = uri;
	filename = m_uri;
}

void HTTPRequest::set_callback(Callback callback) {
	m_callback = callback;
}

std::string HTTPRequest::get_host() const {
	return m_host;
}

unsigned int HTTPRequest::get_port() const {
	return m_port;
}

const std::string& HTTPRequest::get_uri() const {
	return m_uri;
}

const std::string& HTTPRequest::get_filename() const {
	return filename;
}

unsigned int HTTPRequest::get_id() const {
	return m_id;
}

void HTTPRequest::execute() {
	// Ensure that precorditions hold.
	assert(m_port > 0);
	assert(m_host.length() > 0);
	assert(m_uri.length() > 0);
	assert(m_callback != nullptr);

	// Prepare the resolving query.
    // Because the service is specified as a protocol port number(in our case, 3333) 
    // and not as a service name(for example, HTTP, FTP, and so on), we passed the 
    // asio::ip::tcp::resolver::query::numeric_service flag to explicitly inform 
    // the query object about that, so that it properly parses the port number value.
	asio::ip::tcp::resolver::query resolver_query(
        m_host,
		std::to_string(m_port),
		asio::ip::tcp::resolver::query::numeric_service);

	std::unique_lock<std::mutex> cancel_lock(m_cancel_mux);

	if (m_was_cancelled) {
		cancel_lock.unlock();
		on_finish(boost::system::error_code(
			asio::error::operation_aborted));
		return;
	}

	// Resolve the host name.
    // m_resolver is of type asio::ip::tcp::resolver
    // The handler to be called when the resolve operation completes. 
    // Copies will be made of the handler as required. 
    // The function signature of the handler must be:
	/*
	void handler(
		const boost::system::error_code& error, // Result of operation.
		resolver::iterator iterator             // Forward-only iterator that can
		// be used to traverse the list of endpoint entries.
		);
 */
	m_resolver.async_resolve(resolver_query,
		[this](const boost::system::error_code& ec,
		asio::ip::tcp::resolver::iterator iterator)
	{
		on_host_name_resolved(ec, iterator);
	});
}

void HTTPRequest::cancel() {
	std::unique_lock<std::mutex>
		cancel_lock(m_cancel_mux);

	m_was_cancelled = true;

	m_resolver.cancel();

	if (m_sock.is_open()) {
		m_sock.cancel();
	}
}

void HTTPRequest::on_host_name_resolved(
	const boost::system::error_code& ec,
	asio::ip::tcp::resolver::iterator iterator)
{
	if (ec != 0) {
		on_finish(ec);
		return;
	}

	std::unique_lock<std::mutex> cancel_lock(m_cancel_mux);

	if (m_was_cancelled) {
		cancel_lock.unlock();
		on_finish(boost::system::error_code(
			asio::error::operation_aborted));
		return;
	}

	// Connect to the host.
    // This function attempts to connect a socket to one of a sequence of endpoints. 
    // It does this by repeated calls to the socket's async_connect member function, 
    // once for each endpoint in the sequence, until a connection is successfully established.
	asio::async_connect(m_sock, iterator,
		[this](const boost::system::error_code& ec, asio::ip::tcp::resolver::iterator iterator)
	    {
		    on_connection_established(ec, iterator);
	    });

}

void HTTPRequest::on_connection_established(
	const boost::system::error_code& ec,
	asio::ip::tcp::resolver::iterator iterator)
{
		if (ec != 0) {
			on_finish(ec);
			return;
		}

		// Compose the request message.
		m_request_buf += "GET " + m_uri + " HTTP/1.1\r\n";

		// Add mandatory header.
		m_request_buf += "Host: " + m_host + "\r\n";

		m_request_buf += "\r\n";

		std::unique_lock<std::mutex> cancel_lock(m_cancel_mux);

		if (m_was_cancelled) {
			cancel_lock.unlock();
			on_finish(boost::system::error_code(
				asio::error::operation_aborted));
			return;
		}

		// Send the request message.
		asio::async_write(m_sock,
			asio::buffer(m_request_buf),
			[this](const boost::system::error_code& ec,
			std::size_t bytes_transferred)
		{
			on_request_sent(ec, bytes_transferred);
		});
	}

void HTTPRequest::on_request_sent(const boost::system::error_code& ec,
	std::size_t bytes_transferred)
{
		if (ec != 0) {
			on_finish(ec);
			return;
		}

        // Disable sends or receives on the socket.
        // After a request is sent, and if it is sent successfully, 
        // the client application has to let the server know that 
        // the full request is sent and the client is not going to 
        // send anything else by shutting down the send part of the socket.
        // Append EOF on the end
        // basic_stream_socket::shutdown_type:
        // 1) shutdown_receive:Shutdown the receive side of the socket.
		// 2) shutdown_send:Shutdown the send side of the socket.
		// 3) shutdown_both:Shutdown both send and receive on the socket.
        // Disable sends on the socket.
		m_sock.shutdown(asio::ip::tcp::socket::shutdown_send); 

		std::unique_lock<std::mutex> cancel_lock(m_cancel_mux);
		if (m_was_cancelled) {
			cancel_lock.unlock();
			on_finish(boost::system::error_code(
				asio::error::operation_aborted));
			return;
		}

		// Read the status line.
		asio::async_read_until(m_sock,
			m_response.get_response_buf(),
			"\r\n",
			[this](const boost::system::error_code& ec,
			std::size_t bytes_transferred)
		{
			on_status_line_received(ec, bytes_transferred);
		});
	}

void HTTPRequest::on_status_line_received(
	const boost::system::error_code& ec,
	std::size_t bytes_transferred)
{
		if (ec != 0) {
			on_finish(ec);
			return;
		}
		std::cout<<"status line received from cache server"<<std::endl;

		// Parse the status line.
		std::string http_version;
		std::string str_status_code;
		std::string status_message;

		std::istream response_stream(&m_response.get_response_buf());
		response_stream >> http_version;

		if (http_version != "HTTP/1.1"){
			// Response is incorrect.
			on_finish(http_errors::invalid_response);
			return;
		}

		response_stream >> str_status_code;

		// Convert status code to integer.
		unsigned int status_code = 200;

		try {
			status_code = std::stoul(str_status_code);
		}
		catch (std::logic_error&) {
			// Response is incorrect.
			on_finish(http_errors::invalid_response);
			return;
		}

		std::getline(response_stream, status_message, '\r');
		// Remove symbol '\n' from the buffer.
		response_stream.get();

		m_response.set_status_code(status_code);
		m_response.set_status_message(status_message);

		std::unique_lock<std::mutex> cancel_lock(m_cancel_mux);
		if (m_was_cancelled) {
			cancel_lock.unlock();
			on_finish(boost::system::error_code(
				asio::error::operation_aborted));
			return;
		}

		// At this point the status line is successfully received and parsed.
		// Now read the response headers.
        // async_read_until: still sth in the socket
		asio::async_read_until(m_sock,
			m_response.get_response_buf(),
			"\r\n\r\n",
			[this](
			const boost::system::error_code& ec,
			std::size_t bytes_transferred)
		{   
            // read all the data remaining in the socket
            //std::cout<<"x";
			on_headers_received(ec, bytes_transferred); 
		});
	}

void HTTPRequest::on_headers_received(const boost::system::error_code& ec, std::size_t bytes_transferred)
{
		std::cout<<"headers received from cache server"<<std::endl;
		
		if (ec != 0) {
			std::cout<<"ds";
			on_finish(ec);
			return;
		}

		// Parse and store headers.
		std::string header, header_name, header_value;
		std::istream response_stream(&m_response.get_response_buf());

		while (true) {
			std::getline(response_stream, header, '\r');

			// Remove \n symbol from the stream.
			response_stream.get();

			if (header == "")
				break;

			size_t separator_pos = header.find(':');
			if (separator_pos != std::string::npos) {
				header_name = header.substr(0, separator_pos);

				if (separator_pos < header.length() - 1)
					header_value = header.substr(separator_pos + 1);
				else
					header_value = "";

				m_response.add_header(header_name, header_value);
			}
		}

		std::unique_lock<std::mutex> cancel_lock(m_cancel_mux);
		if (m_was_cancelled) {
			cancel_lock.unlock();
			on_finish(boost::system::error_code(asio::error::operation_aborted));
			return;
		}

		// Now we want to read the response body.
		asio::async_read(m_sock,
			m_response.get_response_buf(),
			[this](const boost::system::error_code& ec, std::size_t bytes_transferred)
		    {
		    	//std::cout<<"x";
		    	std::cout<<"ec: "<<ec.message()<<std::endl;
			    on_response_body_received(ec, bytes_transferred);
		    });

		return;
	}

void HTTPRequest::on_response_body_received(
	const boost::system::error_code& ec,
	std::size_t bytes_transferred)
{
		if (ec == asio::error::eof)
			on_finish(boost::system::error_code());
		else
			on_finish(ec);
}

void HTTPRequest::on_finish(const boost::system::error_code& ec)
{
		if (ec != 0) {
			std::cout << "Error occured! Error code = "
					  << ec.value()
					  << ". Message: " << ec.message();
		}

		m_callback(*this, m_response, ec);

		return;
	}

int main()
{
	try {
		HTTPClient client;

		std::shared_ptr<HTTPRequest> request_one = client.create_request(1);

		request_one->set_host("cacheserver");
		request_one->set_uri("/hello");
		request_one->set_port(3333);
		request_one->set_callback(handler);
		
		//std::cout<<request_one->get_filename();

		request_one->execute();

	/*
		std::shared_ptr<HTTPRequest> request_two = client.create_request(1);

		request_two->set_host("localhost");
		request_two->set_uri("/hello");
		request_two->set_port(3333);
		request_two->set_callback(handler);

		request_two->execute();

		//request_two->cancel();
*/
		// Do nothing for 15 seconds, letting the
		// request complete.
		std::this_thread::sleep_for(std::chrono::seconds(15));

		// Closing the client and exiting the application.
		client.close();
	}
	catch (system::system_error &e) {
		std::cout << "Error occured! Error code = " << e.code()
				  << ". Message: " << e.what();

		return e.code().value();
	}

	return 0;
};
