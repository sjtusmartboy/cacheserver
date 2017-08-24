#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

#include <fstream>
#include <atomic>
#include <thread>
#include <iostream>
#include <boost/enable_shared_from_this.hpp>

using namespace boost;

class Service : public boost::enable_shared_from_this<Service>
              , boost::noncopyable
{
	//静态数据，属于所有类的对象共享，只需要一份即可
	static const std::map<unsigned int, std::string> http_status_table;
public:
    typedef boost::shared_ptr<Service> ptr;

public:
	Service(std::shared_ptr<boost::asio::ip::tcp::socket> sock) :
		m_sock(sock),
		m_request(1024),
		m_response_status_code(200), // Assume success.
		m_resource_size_bytes(0)
	{//std::cout<<"dsd";
	};

	void start_handling() {
		//m_sock是shared_ptr，shared_ptr.get得到原生指针指向sock，之后再解引用得到sock对象
		//从sock中读取数据至m_request这个buffer里面，读完以后使用on_request_line_received
		//这个函数分析m_request数据
		//lambda:Class members cannot be captured explicitly by a capture without initializer
		asio::async_read_until(*m_sock.get(), m_request, "\r\n",
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
		std::cout << m_requested_resource <<std::endl;

		std::string request_http_version;
		request_line_stream >> request_http_version;

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

		// Now we have all we need to process the request.
		process_request();
		send_response();

		return;
	}

	void process_request() {
		// Read file. relative path
		std::string resource_file_path = std::string(".") + m_requested_resource;
		std::cout<<resource_file_path;

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
		std::cout<< m_response_headers <<std::endl;
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
			std::cout<<"ec: "<<ec<<std::endl;
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
			asio::ip::address::from_string("192.168.66.129"),
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
			Service::ptr service1 = Service::ptr(new Service(sock));
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
	unsigned short port_num = 8888;

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
