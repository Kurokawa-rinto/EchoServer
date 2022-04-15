#include <mutex>
#include <thread>
#include <utility>

extern "C"
{
#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
}

#include <boost/optional.hpp>

class __echoServer
{
private:
  int sock;
  int listen_port;
  char *server_addr;

public:
  explicit __echoServer () = default;

  int
  argsparse (int argc, char *const argv[])
  {
    int c;
    constexpr const char *optstring = "a:p:";

    while ((c = getopt (argc, argv, optstring)) != -1)
      {
        switch (c)
          {
          case 'a':
            this->server_addr = optarg;
            break;
          case 'p':
            this->listen_port = atoi (optarg);
            break;
          default:
            return -1;
          }
      }

    return 0;
  }

  auto
  preparing (void) -> boost::optional<struct sockaddr_in>
  {
    struct sockaddr_in a_addr;

    this->sock = socket (AF_INET, SOCK_STREAM, 0);

    if (sock == -1)
      {
        fprintf (stderr, "[error] Failed to create a socket.\n");
        return boost::none;
      }

    a_addr.sin_family = AF_INET;
    a_addr.sin_port = htons (static_cast<unsigned short> (this->listen_port));
    a_addr.sin_addr.s_addr = inet_addr (this->server_addr);

    if (bind (sock, (struct sockaddr *)&a_addr, sizeof (a_addr)) < 0)
      {
        fprintf (stderr, "[error] Failed to bind.\n");
        this->finish ();
        return boost::none;
      }

    return a_addr;
  }

  int
  listening (void)
  {
    if (listen (this->sock, 5) == -1)
      {
        this->finish ();
        return -1;
      }

    return 0;
  }

  int
  connecting (struct sockaddr_in *addr, socklen_t *len)
  {
    return accept (this->sock, reinterpret_cast<struct sockaddr *> (addr),
                   len);
  }

  int
  response (int new_sock)
  {
    constexpr size_t bufsize = 4096;

    char buffer[bufsize];

    auto recv_size = recv (new_sock, buffer, bufsize, 0);

    if (recv_size < 0)
      {
        fprintf (stderr, "[error] Failed to read the data form the server.\n");
        return -1;
      }

    auto send_size = send (new_sock, buffer, recv_size, 0);

    if (send_size < 0)
      {
        fprintf (stderr, "[error] Failed to send the server the data.\n");
        return -1;
      }

    return send_size;
  }

  void
  finish (void)
  {
    close (this->sock);
  }
};

class EchoServer
{
private:
  class __echoServer es;
  std::mutex mtx;
  int argc;
  char **argv;

public:
  explicit EchoServer (int __argc, char **__argv) : argc (__argc)
  {
    argv = __argv;
  };

  int
  operator() (void)
  {
    if (es.argsparse (this->argc, this->argv) < 0)
      {
        fprintf (stderr, "[error] missing or invalid options.\n");
        return -1;
      }

    auto ret = es.preparing ();

    if (ret)
      {
        char address[INET_ADDRSTRLEN];

        inet_ntop (AF_INET, &((*ret).sin_addr), address, INET_ADDRSTRLEN);

        auto port = ntohs ((*ret).sin_port);

        fprintf (stdout, "[info] Server address: %s Port: %d\n", address,
                 port);
      }
    else
      {
        return -1;
      }

    if (es.listening () < 0)
      {
        fprintf (stderr, "[error] Failed to wait for connection.\n");
        return -1;
      }

    fprintf (stdout, "[info] Started waiting for connection...\n");

    int result = 0;
    struct sockaddr_in client_addr;

    auto response = [&] (int sock) {
      char ClientAddress[INET_ADDRSTRLEN];

      inet_ntop (AF_INET, &(client_addr.sin_addr), ClientAddress,
                 INET_ADDRSTRLEN);

      auto ClientPort = ntohs (client_addr.sin_port);

      std::lock_guard<std::mutex> lock (this->mtx);

      fprintf (stdout, "[info] Clinet address: %s Client port: %d\n",
               ClientAddress, ClientPort);

      result = es.response (sock);

      fprintf (stdout,
               "[info] The size of the data from the client: %dbytes\n",
               result);

      close (sock);
    };

    while (1)
      {
        std::unique_lock<std::mutex> lock (this->mtx);

        if (result < 0)
          {
            return result;
          }

        lock.unlock ();

        socklen_t len = sizeof (client_addr);

        auto sock = es.connecting (&client_addr, &len);

        std::thread th (response, sock);

        th.detach ();
      }

    return 0;
  }

  ~EchoServer () { es.finish (); }
};

int
main (int argc, char **argv)
{
  class EchoServer es (argc, argv);

  es ();

  return 0;
}

