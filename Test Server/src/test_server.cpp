#include "server_certificate.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/config.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <regex>
#include <map>
#include <mutex>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

std::mutex m;
std::map<std::string, int> quantities;
int cup_count = 12;
auto const address = net::ip::make_address_v4("127.0.0.1");
auto const port = static_cast<unsigned short>(8080);
bool makeDrink = false;
bool orderDone = true;

// Get cup levels
std::string get_cup_levels()
{
  m.lock();
  std::string cups = std::to_string(cup_count);
  m.unlock();
  return cups;
}

// Get quantities
std::string get_quantities(std::string str)
{
  std::string delimeter("?");
  // Get type of liquid from request
  std::string sub(str.substr(str.find(delimeter) + 1, str.length()));
  if (sub == str)
  {
    return std::string("No type sent.");
  }
  // Get quantity level from stored levels for certain type
  return std::to_string(quantities[sub]);
}

// HTTP Response for given request
template<class Body, class Allocator, class Send>
void handle_request(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send)
{
    std::string target(req.target());
    std::regex cups("/cups");
    std::regex quantity("/quantity");
    std::regex order("/order");
    // Create a bad request response
    auto const bad_request =
    [&req](beast::string_view why)
    {
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.keep_alive(req.keep_alive());
        res.body() = std::string(why);
        res.prepare_payload();
        return res;
    };

    // Make sure we can handle the method
    if((req.method() != http::verb::get) && (req.method() != http::verb::options))
    {
      return send(bad_request("Unknown HTTP-method"));
    }

    // Respond to OPTIONS request
    if (req.method() == http::verb::options)
    {
      http::response<http::string_body> res;
      res.version(11);
      res.result(http::status::ok);
      res.set(http::field::server, "SmartBar");
      res.set(http::field::access_control_allow_origin, "*");
      res.set(http::field::access_control_allow_methods, "OPTIONS, GET");
      res.prepare_payload();
      return send(std::move(res));
    }
    // Respond to GET request
    else if (req.method() == http::verb::get)
    {
      if (std::regex_search(target, cups))
      {
        http::response<http::string_body> res;
        res.version(11);
        res.result(http::status::ok);
        res.set(http::field::server, "SmartBar");
        res.body() = get_cup_levels();
        res.prepare_payload();
        return send(std::move(res));
      }
      else if (std::regex_search(target, quantity))
      {
        http::response<http::string_body> res;
        res.version(11);
        res.result(http::status::ok);
        res.set(http::field::server, "SmartBar");
        res.body() = get_quantities(target);
        res.prepare_payload();
        return send(std::move(res));
      }
      else if (std::regex_search(target, order))
      {
        http::response<http::string_body> res;
        res.version(11);
        res.result(http::status::ok);
        res.set(http::field::server, "SmartBar");
        res.body() = "Making Drink...";
        res.prepare_payload();
        m.lock();
        makeDrink = true;
        m.unlock();
        return send(std::move(res));
      }
      else
      {
        return send(bad_request("Unknown Target"));
      }
      
    }

}

// Failure
void fail(beast::error_code ec, char const* what)
{
  std::cerr << what << ": " << ec.message() << "\n";
}

// Lambda function to send HTTP message
template<class Stream>
struct send_lambda
{
  Stream& stream_;
  bool& close_;
  beast::error_code& ec_;

  explicit send_lambda(Stream& stream, bool& close, beast::error_code& ec) : stream_(stream), close_(close), ec_(ec)
  {
  }

  template<bool isRequest, class Body, class Fields>
  void operator()(http::message<isRequest, Body, Fields>&& msg) const
  {
    // Should we close connection
    close_ = msg.need_eof();

    // Serializer for const messages
    http::serializer<isRequest, Body, Fields> sr{msg};
    http::write(stream_, sr, ec_);
  }
};

// Handle HTTP server connection
void do_session(tcp::socket& socket/*, ssl::context& ctx*/)
{
  bool close = false;
  beast::error_code ec;

  // ssl::stream<tcp::socket&> stream{socket, ctx};

  // stream.handshake(ssl::stream_base::server, ec);
  // if (ec)
  // {
  //   return fail(ec, "handshake");
  // }


  // Buffer to persist across reads
  beast::flat_buffer buffer;

  // Lambda to send messages
  send_lambda<tcp::socket/*ssl::stream<tcp::socket&>*/> lambda{socket/*stream*/, close, ec};

  for(;;)
  {
    // Read a request
    http::request<http::string_body> req;
    http::read(socket/*stream*/, buffer, req, ec);
    if (ec == http::error::end_of_stream)
    {
      break;
    }
    if (ec)
    { 
      return fail(ec, "read");
    }
    else
    {
      // Write the message to standard out
      std::cout << req << std::endl;
    }
    

    // Send Response
    handle_request(std::move(req), lambda);
    if (ec)
    {
      return fail(ec, "write");
    }
    if (close)
    {
      break;
    }
  }

  // Send SSL shutdown
  // stream.shutdown(ec);
  socket.shutdown(tcp::socket::shutdown_send, ec);
  // if (ec)
  // {
  //   return fail(ec, "shutdown");
  // }
}

int start_server()
{
  try
  {
    net::io_context ioc{1};

    //ssl::context ctx(ssl::context::tlsv12);

    //load_server_certificate(ctx);

    // Create Acceptor
    tcp::acceptor acceptor_{ioc, {address, port}};

    // for(;;)
    // {
    //   // Receive new connection
    //   tcp::socket socket{ioc};

    //   // Block until connection
    //   acceptor_.accept(socket);

    //   std::thread{std::bind(
    //     &do_session, 
    //     std::move(socket)
    //     /*,std::ref(ctx)*/
    //   )}.detach();
    // }
    tcp::socket socket{ioc};
    acceptor_.accept(socket);
    do_session(socket);
  }
  catch(const std::exception& e)
  {
    std::cerr << e.what() << '\n';
  }
}

int make_drink()
{
  m.lock();
  orderDone = false;
  std::cout << "Making Drink..." << std::endl;
  std::cout << "Moving Motors..." << std::endl;
  std::cout << "Delivering Drink..." << std::endl;
  cup_count--;
  makeDrink = false;
  orderDone = true;
  m.unlock();
}

int main(int argc, char* argv[])
{
  // Set drink amounts
  quantities.insert(std::pair<std::string, int>("Whiskey", 60));
  quantities.insert(std::pair<std::string, int>("Vodka", 60));
  quantities.insert(std::pair<std::string, int>("Rum", 60));
  quantities.insert(std::pair<std::string, int>("Coke", 33));
  quantities.insert(std::pair<std::string, int>("Sprite", 33));
  quantities.insert(std::pair<std::string, int>("Cranberry", 33));

  // std::thread server(start_server);
  // server.join();
  for(;;)
  {
    start_server();
    if ((makeDrink == true) && (orderDone == true))
    {
      std::thread(make_drink).detach();
    }
  }
}