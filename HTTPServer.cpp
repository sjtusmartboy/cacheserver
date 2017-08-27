#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

#include <fstream>
#include <atomic>
#include <thread>
#include <iostream>
#include <boost/enable_shared_from_this.hpp>
#include <mutex>
#include <memory>
#include <boost/bind.hpp>
#include <fstream>
#include <boost/asio.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <condition_variable>

#include <unistd.h>
#include "HTTPResponse.h"
//#include <boost/make_shared.hpp>

//#include "./client/HTTPClient.h"

using namespace boost;
using namespace boost::asio;

class Service : public boost::enable_shared_from_this<Service>
              , boost::noncopyable
{
	//静态数据，属于所有类的对象共享，只需要一份即可
	static const std::map<unsigned int, std::string> http_status_table;
public:
    typedef boost::shared_ptr<Service> ptr;

public:
	Service(std::shared_ptr<boost::asio::ip::tcp::socket> sock,asio::io_service& ios) :
		m_sock(sock),
		m_request(1024),
		m_response_status_code(200), // Assume success.
		m_resource_size_bytes(0),
		m_ios(ios),
		m_resolver(ios),
		m_was_cancelled(false),
		m_response_buf_backup(1024)
		//lck(mtx)
	{//std::cout<<"dsd";
	//std::shared_ptr<asio::ip::tcp::socket> sock(new asio::ip::tcp::socket(m_ios));
		//std::shared_ptr<boost::asio::ip::tcp::socket> m_remotesock;
		m_remotesock = std::shared_ptr<boost::asio::ip::tcp::socket>(new asio::ip::tcp::socket(m_ios));
		//std::cout<<"count1="<<m_remotesock.use_count()<<std::endl;
		//lck = std::unique_lock<std::mutex> ;
		
	};

	void start_handling() {
		//std::cout<<"count2="<<m_remotesock.use_count()<<std::endl;
		//m_sock是shared_ptr，shared_ptr.get得到原生指针指向sock，之后再解引用得到sock对象
		//从sock中读取数据至m_request这个buffer里面，读完以后使用on_request_line_received
		//这个函数分析m_request数据
		//lambda:Class members cannot be captured explicitly by a capture without initializer
		asio::async_read_until(
			*m_sock.get(), 
			m_request, 
			//buffer(read_buffer_),
			"\r\n",
			[this,ha = shared_from_this()](const boost::system::error_code& ec, std::size_t bytes_transferred)
			{
			//std::cout<<"dd";
				on_request_line_received(ec,bytes_transferred);
			});
	}

private:
	void on_request_line_received(const boost::system::error_code& ec, std::size_t bytes_transferred)
	{
		if (ec != 0) {
		
			std::cout << "Error occured! Error code = "
					  << ec.value()
					  << ". Message: " << ec.message();

			if (ec == asio::error::not_found) {
				// No delimiter has been fonud in the request message.
				m_response_status_code = 413; // Request Entity Too Large（请求实体太大）
				send_response();
				return;
			}
			else {
				// In case of any other error ? close the socket and clean up.
				on_finish();
				return;
			}
		}
		

		//m_request缓冲区里的内容是"GET /index.html HTTP/1.1\r\nHost: localhost\r\n";

		// Parse the request line.
		std::string request_line;
		//将boost::asio::streambuf类型的m_request转化为std::istream类型的request_stream
		std::istream request_stream(&m_request);
		//std::istream request_stream(&buffer_between_client_and_cacheserver);
		//istream& getline (istream& is, string& str, char delim);
		//Extracts characters from is and stores them into str until the delimitation
		//character delim is found (or the newline character, '\n', for (2)).
		//getline reads characters from an input stream and places them into a string
		//从输入流request_stream获取后在request_stream流里会移除request_line
		//request_line内容是GET /index.html HTTP/1.1
		std::getline(request_stream, request_line, '\r');
		// Remove symbol '\n' from the buffer.
		//request_stream所指向的缓冲区内容为Host: localhost\r\n
		request_stream.get();

		// Parse the request line.
		std::string request_method;
		//istringstream作用是从string对象request_line中读取字符。
		std::istringstream request_line_stream(request_line);
		request_line_stream >> request_method;
		//request_stream内容为index.html HTTP/1.1



		// We only support GET method.
		if (request_method.compare("GET") != 0) {
			// Unsupported method.
			m_response_status_code = 501;
			send_response();

			return;
		}

		request_line_stream >> m_requested_resource;
		//request_stream内容为HTTP/1.1

		std::string request_http_version;
		request_line_stream >> request_http_version;
		std::cout<<request_http_version<<std::endl;

		if (request_http_version.compare("HTTP/1.1") != 0) {
			// Unsupported HTTP version or bad request.
			m_response_status_code = 505;
			send_response();
			return;
		}

		// At this point the request line is successfully
		// received and parsed. Now read the request headers.
		asio::async_read_until(*m_sock.get(),
			m_request,
			"\r\n\r\n",
			[this,ha = shared_from_this()](const boost::system::error_code& ec, std::size_t bytes_transferred)
		{
		//std::cout<<"grr";
			on_headers_received(ec, bytes_transferred);
		});

		return;
	}

	void on_headers_received(const boost::system::error_code& ec, std::size_t bytes_transferred)
	{
		if (ec != 0) {
			std::cout << "Error occured! Error code = "
				<< ec.value()
				<< ". Message: " << ec.message();

			if (ec == asio::error::not_found) {
				// No delimiter has been fonud in the
				// request message.

				m_response_status_code = 413;
				send_response();
				return;
			}
			else {
				// In case of any other error - close the
				// socket and clean up.
				on_finish();
				return;
			}
		}
		//std::cout<<"gdr";

		// Parse and store headers.
		std::istream request_stream(&m_request);
		std::string header_name, header_value;

		//Host: localhost
		//Connection: Keep-Alive
		while (!request_stream.eof()) {
			std::getline(request_stream, header_name, ':');
			if (!request_stream.eof()) {
				std::getline(request_stream, header_value, '\r');

				// Remove symbol \n from the stream.
				request_stream.get();
				m_request_headers[header_name] = header_value;
			}
		}
		/*--------------------------------------------------------------------------------------------------*/
		//std::cout<<"a";
		if (file_exist("hello") && check_freshment()){
			// Now we have all we need to process the request.
			process_request();
			send_response();
		} else {
			//std::cout<<"c"<<std::endl;
			//establish_remoteServer();
			asio::ip::tcp::resolver::query resolver_query(
        		"remoteserver",
				std::to_string(8888),
				asio::ip::tcp::resolver::query::numeric_service);
			
			m_resolver.async_resolve(resolver_query,
				[this,ha = shared_from_this()](const boost::system::error_code& ec,
					asio::ip::tcp::resolver::iterator iterator)
					{
						//std::cout<<"d"<<std::endl;
						on_host_name_resolved(ec, iterator);
					});
		}
		return;
	}
	
	void on_host_name_resolved( const boost::system::error_code& ec,
		asio::ip::tcp::resolver::iterator iterator)
	{
		if (ec != 0) {
			std::cout<<"e"<<std::endl;
			//on_finish(ec);
			std::cout << "Error occured! Error code = "
				<< ec.value()
				<< ". Message: " << ec.message()<<std::endl;
			return;
		}
		

		std::unique_lock<std::mutex> cancel_lock(m_cancel_mux);

		if (m_was_cancelled) {
			cancel_lock.unlock();
			//on_finish(boost::system::error_code(
			//	asio::error::operation_aborted));
			return;
		}

		// Connect to the host.
		// This function attempts to connect a socket to one of a sequence of endpoints. 
		// It does this by repeated calls to the socket's async_connect member function, 
		// once for each endpoint in the sequence, until a connection is successfully established.
		asio::async_connect(*m_remotesock.get(), iterator,
			[this,ha = shared_from_this()](const boost::system::error_code& ec, asio::ip::tcp::resolver::iterator iterator)
			{
				//std::cout<<"dddddddddd";
				on_connection_established(ec, iterator);
			});
	}
	
	void on_connection_established(const boost::system::error_code& ec,
		asio::ip::tcp::resolver::iterator iterator)
	{
		if (ec != 0) {
			//on_finish(ec);
			return;
		}

		// Compose the request message.
		m_request_buf += "GET " + m_requested_resource.erase(0,1) + " HTTP/1.1\r\n";
		std::cout<<m_request_buf<<std::endl;
		std::cout << m_request_buf <<std::endl;

		// Add mandatory header.
		m_request_buf += "Host: " + m_request_headers["Host: "] + "\r\n";
		m_request_buf += "\r\n";

		std::unique_lock<std::mutex> cancel_lock(m_cancel_mux);
		if (m_was_cancelled) {
			cancel_lock.unlock();
			//on_finish(boost::system::error_code(
			//	asio::error::operation_aborted));
			return;
		}

		// Send the request message.
		asio::async_write(*m_remotesock.get(),
			asio::buffer(m_request_buf),
			[this,ha = shared_from_this()](const boost::system::error_code& ec,
			std::size_t bytes_transferred)
		{
			//std::cout<<"g"<<std::endl;
			on_request_sent(ec, bytes_transferred);
		});
	}
	
	void on_request_sent(const boost::system::error_code& ec,
		std::size_t bytes_transferred)
	{
		if (ec != 0) {
			//on_finish(ec);
			return;
		}

        // Disable sends or receives on the socket.
        // After a request is sent, and if it is sent successfully, 
        // the client application has to let the server know that 
        // the full request is sent and the client is not going to 
        // send anything else by shutting down the send part of the socket.
        // basic_stream_socket::shutdown_type:
        // 1) shutdown_receive:Shutdown the receive side of the socket.
		// 2) shutdown_send:Shutdown the send side of the socket.
		// 3) shutdown_both:Shutdown both send and receive on the socket.
        // Disable sends on the socket.
		m_remotesock->shutdown(asio::ip::tcp::socket::shutdown_send); 

		std::unique_lock<std::mutex> cancel_lock(m_cancel_mux);
		if (m_was_cancelled) {
			cancel_lock.unlock();
			//on_finish(boost::system::error_code(
				//asio::error::operation_aborted));
			return;
		}

		// Read the status line from remote_socket.
		asio::async_read_until(*m_remotesock.get(),
			m_response.get_response_buf(),
			"\r\n",
			[this,ha = shared_from_this()](const boost::system::error_code& ec,
			std::size_t bytes_transferred)
		{
			//std::cout<<"H"<<ec<<std::endl;
			on_status_line_received(ec, bytes_transferred);
		});

	}
	
	void on_status_line_received(const boost::system::error_code& ec,
		std::size_t bytes_transferred)
	{
		if (ec != 0) {
			//on_finish(ec);
			return;
		}
		
		// Parse the status line.
		std::string http_version;
		std::string str_status_code;
		std::string status_message;

		std::istream response_stream(&m_response.get_response_buf());
		response_stream >> http_version;
		
		//std::cout<<http_version<<std::endl;

		if (http_version != "HTTP/1.1"){
			// Response is incorrect.
			//on_finish(http_errors::invalid_response);
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
			//on_finish(http_errors::invalid_response);
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
			//on_finish(boost::system::error_code(
				//asio::error::operation_aborted));
			return;
		}

		// At this point the status line is successfully received and parsed.
		// Now read the response headers.
        // async_read_until: still sth in the socket
		asio::async_read_until(*m_remotesock.get(),
			m_response.get_response_buf(),
			"\r\n\r\n",
			[this,ha = shared_from_this()](
			const boost::system::error_code& ec,
			std::size_t bytes_transferred)
		{   
            // read all the data remaining in the socket
            //std::cout<<"ec1: "<<ec.message()<<std::endl;
			on_headers_received_from_remoteserver(ec, bytes_transferred); 
		});
				
		// give feedback to the client
		auto status_line = http_status_table.at(m_response_status_code);
		m_response_status_line = std::string("HTTP/1.1 ") + status_line + "\r\n";
		//std::cout<<m_response_status_line<<std::endl;

		std::vector<asio::const_buffer> response_buffers;
		response_buffers.push_back(asio::buffer(m_response_status_line));

		// Write the status line to the client.
		asio::async_write(*m_sock.get(),
			response_buffers,
			[this,ha = shared_from_this()](const boost::system::error_code& ec, std::size_t bytes_transferred)
		{
			//do nothing
			//std::cout<<"status line sent to client"<<std::endl;
			//std::cout<<"ec2: "<<ec<<std::endl;
		});
	}

	void on_headers_received_from_remoteserver(const boost::system::error_code& ec, std::size_t bytes_transferred)
	{
		if (ec != 0) {
			std::cout<<"ds"<<std::endl;
			//on_finish(ec);
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
				m_response_headers += header_name + ": " + header_value + "\r\n";
				//std::cout<<"header_name: "<<header_name<< "header_value: "<<header_value<<std::endl;
				std::cout<<m_response_headers<<std::endl;
			}
		}
		m_response_headers += "\r\n";
		
		asio::async_write(*m_sock.get(),
			asio::buffer(m_response_headers),
			[this,ha = shared_from_this()](const boost::system::error_code& ec, std::size_t bytes_transferred)
		{
			//std::cout<<"ec3: "<<ec.message()<<std::endl;
			std::cout<<"headers sent to client"<<std::endl;
			//on_response_sent(ec, bytes_transferred);
		});

		std::unique_lock<std::mutex> cancel_lock(m_cancel_mux);
		if (m_was_cancelled) {
			cancel_lock.unlock();
			//on_finish(boost::system::error_code(asio::error::operation_aborted));
			return;
		}
		
		//The program must ensure that the stream performs no other write operations (such as async_write, the 
		//stream's async_write_some function, or any other composed operations that perform writes) until this
		//operation completes.
		async_read(*m_remotesock.get(),
	        //boost::asio::buffer(read_buffer_),
	        m_response.get_response_buf(),
			//boost::bind(&Service::read_complete, shared_from_this(),_1,_2),
			[this,ha = shared_from_this()](const boost::system::error_code& ec, std::size_t bytes_transferred)
			{

				//std::cout<<"ec4: "<<ec.message()<<std::endl;
				std::size_t bytes_copied = buffer_copy(
					m_response_buf_backup.prepare(m_response.get_response_buf().size()), // target's output sequence
					m_response.get_response_buf().data());                // source's input sequence

				// Explicitly move target's output sequence to its input sequence.
				m_response_buf_backup.commit(bytes_copied);
				//std::cout<<"bytes_copied: "<<bytes_copied<<std::endl;
				
				std::istream ism(&m_response_buf_backup);
				std::istreambuf_iterator<char> eos;
				std::string str(std::istreambuf_iterator<char>(ism), eos);
				//std::cout<< "str: "<<str<< std::endl;
				
				process(ec,bytes_transferred);
			});
		return;
	}
	
   size_t read_complete(const boost::system::error_code & err, size_t bytes) {
        if ( err == asio::error::eof ) {
        	return 0;
        }else{
       		return 1;
        }
    }
    
    void process(const boost::system::error_code& ec, std::size_t bytes_transferred){
		/* 
		// this piece of code is used to get the data from the streambuf
		// extremely useful in debugging
		std::istream ism(&m_response_buf_backup);
		std::istreambuf_iterator<char> eos;
		std::string str(std::istreambuf_iterator<char>(ism), eos);
		std::cout<< "str: "<<str<< std::endl;
		*/
    	
    	async_write(*m_sock.get(),
    		m_response_buf_backup,
    		//boost::bind(&Service::read_complete, shared_from_this(),_1,_2),
    		[this,ha = shared_from_this()](const boost::system::error_code& ec, std::size_t bytes_transferred)
			{
					std::lock_guard<std::mutex> lk(mtx);
					data_cond.notify_one();
					
					//std::cout<<"ec5: "<<ec.message()<<std::endl;
					if(ec == asio::error::eof){ // never executes
						m_sock->shutdown(asio::ip::tcp::socket::shutdown_both);						
						//std::cout<<"end of writing"<<std::endl;
					}	
					else{// executes this branch, which does nothing
						//std::cout<<"writing to the client"<<std::endl;
					}
			});
			
		async_read(*m_remotesock.get(),
			m_response.get_response_buf(),
			boost::bind(&Service::read_complete, shared_from_this(),_1,_2),
			[this,ha = shared_from_this()](const boost::system::error_code& ec, std::size_t bytes_transferred)
			{
				std::unique_lock<std::mutex> lk(mtx);
				data_cond.wait(lk,[this]{ 
					//std::cout<<"size:  "<<m_response_buf_backup.size()<<std::endl;
					return ! m_response_buf_backup.size(); });

				std::size_t bytes_copied = buffer_copy(
					m_response_buf_backup.prepare(m_response.get_response_buf().size()), // target's output sequence
					m_response.get_response_buf().data());                // source's input sequence
				// Explicitly move target's output sequence to its input sequence.
				m_response_buf_backup.commit(bytes_copied);
				
				std::istream ism(&m_response.get_response_buf());
				std::istreambuf_iterator<char> eos;
				std::string str(std::istreambuf_iterator<char>(ism), eos);
				std::cout<< "str: "<<str<< std::endl;
				filebuf += str;
				
				//std::cout<<"read finish"<<std::endl;
				if(ec == 0){
					process(ec,bytes_transferred);
				}
				else{ //if(ec == asio::error::eof){
					//std::cout<<"end of reading"<<std::endl;
					async_write(*m_sock.get(),
						m_response_buf_backup, //write the final piece of data from m_response_buf_backup to client
						//boost::bind(&Service::read_complete, shared_from_this(),_1,_2),
						[this,ha = shared_from_this()](const boost::system::error_code& ec, std::size_t bytes_transferred)
						{
							//std::cout<<"end of sending last"<<std::endl;
							m_sock->shutdown(asio::ip::tcp::socket::shutdown_both); //close connection with the client
						});
					
					std::ofstream resource_fstream("."+m_requested_resource, std::ifstream::binary);
					//std::cout<< m_requested_resource <<std::endl;
					resource_fstream << filebuf;
				}
			});
    
    }
	
	
	void on_response_body_received(const boost::system::error_code& ec,
		std::size_t bytes_transferred)
	{
		std::cout<<"k"<<std::endl;
		if (ec == asio::error::eof)
			on_finish(boost::system::error_code());
		else{std::cout<<"/"<<std::endl;on_finish(ec);}
			
	}

	void on_finish(const boost::system::error_code& ec)
	{
		if (ec != 0) {
			std::cout << "Error occured! Error code = "
					  << ec.value()
					  << ". Message: " << ec.message();
		}

		
		//m_callback(*this, m_response, ec);
		std::string filepath = std::string(".") + m_requested_resource;
			std::ofstream ofs(filepath, std::ofstream::out);
			ofs << m_response.get_response().rdbuf(); 
			ofs.close();

		return;
	}
	
	bool check_freshment(){
	
		return false;
	}
	
	bool file_exist(std::string file_name){
	
		return true;
	}
	
	/*-----------------------------------------------------------------------------------------------------*/

	void process_request() {
		// Read file. relative path
		std::string resource_file_path = std::string("/home/sjtusmartboy/Documents/boostProject") + m_requested_resource;
		//std::cout<<resource_file_path;

		if (!boost::filesystem::exists(resource_file_path)) {
			// Resource not found.
			m_response_status_code = 404;
			return;
		}

		std::ifstream resource_fstream(resource_file_path, std::ifstream::binary);

		if (!resource_fstream.is_open()) {
			// Could not open file. 
			// Something bad has happened.
			m_response_status_code = 500;
			return;
		}

		// Find out file size.
		resource_fstream.seekg(0, std::ifstream::end);
		m_resource_size_bytes = static_cast<std::size_t>(resource_fstream.tellg());

		m_resource_buffer.reset(new char[m_resource_size_bytes]);

		resource_fstream.seekg(std::ifstream::beg);
		resource_fstream.read(m_resource_buffer.get(), m_resource_size_bytes);

		m_response_headers += std::string("content-length") + ": " + 
							  std::to_string(m_resource_size_bytes) + "\r\n";
	}

	void send_response()  {
		m_sock->shutdown(asio::ip::tcp::socket::shutdown_receive);

		auto status_line = http_status_table.at(m_response_status_code);

		m_response_status_line = std::string("HTTP/1.1 ") + status_line + "\r\n";

		m_response_headers += "\r\n";

		std::vector<asio::const_buffer> response_buffers;
		response_buffers.push_back(asio::buffer(m_response_status_line));

		if (m_response_headers.length() > 0) {
			response_buffers.push_back(
				asio::buffer(m_response_headers));
		}

		if (m_resource_size_bytes > 0) {
			response_buffers.push_back(asio::buffer(m_resource_buffer.get(), m_resource_size_bytes));
		}

		// Initiate asynchronous write operation.
		asio::async_write(*m_sock.get(),
			response_buffers,
			[this,ha = shared_from_this()](const boost::system::error_code& ec, std::size_t bytes_transferred)
		{
			//std::cout<<"rhtr";
			on_response_sent(ec, bytes_transferred);
		});
	}

	void on_response_sent(const boost::system::error_code& ec, std::size_t bytes_transferred)
	{
		if (ec != 0) {
			std::cout << "Error occured! Error code = "
					  << ec.value()
					  << ". Message: " << ec.message();
		}
		std::cout<<"hi"<<std::endl;

		m_sock->shutdown(asio::ip::tcp::socket::shutdown_both);
		on_finish();
	}

	// Here we perform the cleanup.
	void on_finish() {
		//delete this;
	}
	
	void cancel() {
		std::unique_lock<std::mutex> cancel_lock(m_cancel_mux);

		m_was_cancelled = true;

		m_resolver.cancel();

		if (m_remotesock->is_open()) {
			m_remotesock->cancel();
		}
	}


private:
	std::shared_ptr<boost::asio::ip::tcp::socket> m_sock;
	boost::asio::streambuf m_request;
	std::map<std::string, std::string> m_request_headers;
	std::string m_requested_resource;

	std::unique_ptr<char[]> m_resource_buffer;
	unsigned int m_response_status_code;
	std::size_t m_resource_size_bytes;
	std::string m_response_headers;
	std::string m_response_status_line;
	
	std::shared_ptr<boost::asio::ip::tcp::socket> m_remotesock;
	asio::ip::tcp::resolver m_resolver;
	asio::io_service& m_ios;
	std::mutex m_cancel_mux;
	bool m_was_cancelled;
	std::string m_request_buf;
	HTTPResponse m_response;
	asio::streambuf m_response_buf_backup;
	
		//enum { max_msg = 1024 };
	//const int	max_msg = 1024 ;
    char read_buffer_[1024];
    char write_buffer_[1024];
    char buffer_between_client_and_cacheserver[1024];
    char buffer_between_cacheserver_and_server[1024];
    
    std::mutex mtx;
    std::unique_lock<std::mutex> lck;
    //mutable boost::shared_mutex mutex_;
    std::string filebuf;
    mutable boost::shared_mutex entry_mutex;
    //boost::shared_lock<boost::shared_mutex> lk;
    std::condition_variable data_cond;

};

const std::map<unsigned int, std::string>
Service::http_status_table =
{
	{ 200, "200 OK" },
	{ 404, "404 Not Found" },
	{ 413, "413 Request Entity Too Large" },
	{ 500, "500 Server Error" },
	{ 501, "501 Not Implemented" },
	{ 505, "505 HTTP Version Not Supported" }
};

class Acceptor {
public:
	Acceptor(asio::io_service& ios, unsigned short port_num) :
		m_ios(ios),
		m_acceptor(m_ios,
		asio::ip::tcp::endpoint(
		asio::ip::address_v4::any(),
		port_num)),
		m_isStopped(false)
	{}

	// Start accepting incoming connection requests.
	void Start() {
		m_acceptor.listen();
		InitAccept();
	}

	// Stop accepting incoming connection requests.
	void Stop() {
		m_isStopped.store(true);
	}

private:
	void InitAccept() {
		std::shared_ptr<asio::ip::tcp::socket> sock(new asio::ip::tcp::socket(m_ios));

		m_acceptor.async_accept(*sock.get(),
			[this, sock](const boost::system::error_code& error)
			{
				onAccept(error, sock);
			});
	}

	void onAccept(const boost::system::error_code& ec,
		std::shared_ptr<asio::ip::tcp::socket> sock)
	{
		if (ec == 0) {
			//std::cout<<"ds";
			Service::ptr service1 = Service::ptr(new Service(sock,m_ios));
			service1->start_handling();
			//std::cout<<"ewe";
			//(new Service(sock))->start_handling();
		}
		else {
			std::cout << "Error occured! Error code = "
				<< ec.value()
				<< ". Message: " << ec.message();
		}

		// Init next async accept operation if
		// acceptor has not been stopped yet.
		if (!m_isStopped.load()) {
			InitAccept();
		}
		else {
			// Stop accepting incoming connections
			// and free allocated resources.
			m_acceptor.close();
		}
	}

private:
	asio::io_service& m_ios;
	asio::ip::tcp::acceptor m_acceptor;
	std::atomic<bool> m_isStopped;
};

class Server {
public:
	Server() {
		// The work class is used to inform the io_service when work starts and finishes. 
		// This ensures that the io_service object's run() function will not exit 
		// while work is underway, and that it does exit when there is no unfinished work remaining.
		m_work.reset(new asio::io_service::work(m_ios));
	}

	// Start the server.
	void Start(unsigned short port_num, unsigned int thread_pool_size) {

		assert(thread_pool_size > 0);

		// Create and strat Acceptor.
		acc.reset(new Acceptor(m_ios, port_num));
		acc->Start();

		// Create specified number of threads and add them to the pool.
		for (unsigned int i = 0; i < thread_pool_size; i++) {
			std::unique_ptr<std::thread> th(
				new std::thread([this]()
				{
					m_ios.run();
				}));

			m_thread_pool.push_back(std::move(th));
		}
	}

	// Stop the server.
	void Stop() {
		acc->Stop();
		m_ios.stop();

		for (auto& th : m_thread_pool) {
			th->join();
		}
	}

private:
	asio::io_service m_ios;
	std::unique_ptr<asio::io_service::work> m_work;
	std::unique_ptr<Acceptor> acc;
	std::vector<std::unique_ptr<std::thread>> m_thread_pool;
};

const unsigned int DEFAULT_THREAD_POOL_SIZE = 2;

int main()
{
	unsigned short port_num = 3333;
	
	if (daemon(0, 0) == -1) {
        perror("daomon error");
        exit(EXIT_FAILURE);
    }


	try {
		Server srv;
		//std::cout<<"dsd1";

		unsigned int thread_pool_size = std::thread::hardware_concurrency() * 2;

		if (thread_pool_size == 0)
			thread_pool_size = DEFAULT_THREAD_POOL_SIZE;
		//std::cout<<"dsd1";
		srv.Start(port_num, thread_pool_size);

		std::this_thread::sleep_for(std::chrono::seconds(60));

		srv.Stop();
	}
	catch (system::system_error &e) {
		std::cout << "Error occured! Error code = "
			<< e.code() << ". Message: "
			<< e.what();
	}

	return 0;
}
