// Connection.cpp
//
// Implementation of the plain-TCP framed connection. See Connection.h.

#include "Connection.h"

#include <cerrno>
#include <cstring>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace campiello {
namespace net {

namespace {

// Write the whole buffer, retrying short writes and EINTR.
bool WriteAll(int fd, const uint8_t* data, size_t length)
{
	size_t offset = 0;
	while (offset < length) {
		ssize_t n = ::send(fd, data + offset, length - offset, 0);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		if (n == 0)
			return false;
		offset += (size_t)n;
	}
	return true;
}

} // namespace

Connection::~Connection()
{
	Close();
}

Connection::Connection(Connection&& other) noexcept
	: fFd(other.fFd), fParser(std::move(other.fParser)), fError(other.fError)
{
	other.fFd = -1;
}

Connection& Connection::operator=(Connection&& other) noexcept
{
	if (this != &other) {
		Close();
		fFd = other.fFd;
		fParser = std::move(other.fParser);
		fError = other.fError;
		other.fFd = -1;
	}
	return *this;
}

void Connection::Close()
{
	if (fFd >= 0) {
		::close(fFd);
		fFd = -1;
	}
}

void Connection::Shutdown()
{
	if (fFd >= 0)
		::shutdown(fFd, SHUT_RDWR);
}

bool Connection::Send(const wire::Frame& frame)
{
	if (fFd < 0) {
		fError = "connection closed";
		return false;
	}
	std::vector<uint8_t> bytes;
	if (!wire::EncodeFrame(frame, bytes)) {
		fError = "frame payload too large to encode";
		return false;
	}
	if (!WriteAll(fFd, bytes.data(), bytes.size())) {
		fError = "write error";
		return false;
	}
	return true;
}

bool Connection::ReadMore()
{
	uint8_t buffer[4096];
	for (;;) {
		ssize_t n = ::recv(fFd, buffer, sizeof(buffer), 0);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			fError = "read error";
			return false;
		}
		if (n == 0) {
			fError = "connection closed by peer";
			return false;
		}
		fParser.Feed(buffer, (size_t)n);
		return true;
	}
}

bool Connection::Receive(wire::Frame& out)
{
	if (fFd < 0) {
		fError = "connection closed";
		return false;
	}
	for (;;) {
		wire::ParseResult result = fParser.Next(out);
		if (result == wire::ParseResult::kFrame)
			return true;
		if (result == wire::ParseResult::kError) {
			fError = fParser.ErrorMessage();
			return false;
		}
		// kNeedMore: pull more bytes from the socket.
		if (!ReadMore())
			return false;
	}
}

int TcpConnect(const char* host, uint16_t port)
{
	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		::close(fd);
		return -1;
	}
	if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		::close(fd);
		return -1;
	}
	return fd;
}

bool Connect(const char* host, uint16_t port, Connection& out)
{
	int fd = TcpConnect(host, port);
	if (fd < 0)
		return false;
	out = Connection(fd);
	return true;
}

Listener::~Listener()
{
	Close();
}

void Listener::Close()
{
	if (fFd >= 0) {
		::close(fFd);
		fFd = -1;
	}
}

bool Listener::Listen(const char* host, uint16_t port)
{
	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return false;

	int yes = 1;
	::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		::close(fd);
		return false;
	}
	if (::bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		::close(fd);
		return false;
	}
	if (::listen(fd, 4) < 0) {
		::close(fd);
		return false;
	}
	fFd = fd;
	return true;
}

int Listener::AcceptRawFd()
{
	if (fFd < 0)
		return -1;
	return ::accept(fFd, nullptr, nullptr);
}

bool Listener::Accept(Connection& out)
{
	int fd = AcceptRawFd();
	if (fd < 0)
		return false;
	out = Connection(fd);
	return true;
}

uint16_t Listener::Port() const
{
	if (fFd < 0)
		return 0;
	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);
	if (::getsockname(fFd, (struct sockaddr*)&addr, &len) != 0)
		return 0;
	return ntohs(addr.sin_port);
}

} // namespace net
} // namespace campiello
