
#include "Config.h"
#include "NetworkThread.h"
#include "ScriptMgr.h"
#include "ToolSocket.h"
#include "ToolSocketMgr.h"

#include <boost/system/error_code.hpp>

static void OnToolSocketAccept(tcp::socket&& sock, uint32 threadIndex)
{
	sToolSocketMgr.OnSocketOpen(std::forward<tcp::socket>(sock), threadIndex);
}

ToolSocketMgr::ToolSocketMgr() : BaseSocketMgr(), _socketSendBufferSize(-1), m_SockOutUBuff(65536), _tcpNoDelay(true)
{
}

ToolSocketMgr& ToolSocketMgr::Instance()
{
	static ToolSocketMgr instance;
	return instance;
}

bool ToolSocketMgr::StartNetwork(boost::asio::io_service& service, std::string const& bindIp, uint16 port, int threadCount)
{
	_tcpNoDelay = sConfigMgr->GetBoolDefault("Network.TcpNodelay", true);

	int const max_connections = boost::asio::socket_base::max_connections;
	TC_LOG_DEBUG(LOG_FILTER_GENERAL, "Max allowed socket connections %d", max_connections);

	// -1 means use default
	_socketSendBufferSize = sConfigMgr->GetIntDefault("Network.OutKBuff", -1);

	m_SockOutUBuff = sConfigMgr->GetIntDefault("Network.OutUBuff", 65536);

	if (m_SockOutUBuff <= 0)
	{
		TC_LOG_ERROR(LOG_FILTER_GENERAL, "Network.OutUBuff is wrong in your config file");
		return false;
	}

	BaseSocketMgr::StartNetwork(service, bindIp, port, threadCount);

	_acceptor->SetSocketFactory(std::bind(&BaseSocketMgr::GetSocketForAccept, this));

	_acceptor->AsyncAcceptWithCallback<&OnToolSocketAccept>();
	return true;
}

void ToolSocketMgr::StopNetwork()
{
	BaseSocketMgr::StopNetwork();
}

void ToolSocketMgr::OnSocketOpen(tcp::socket&& sock, uint32 threadIndex)
{
	// set some options here
	if (_socketSendBufferSize >= 0)
	{
		boost::system::error_code err;
		sock.set_option(boost::asio::socket_base::send_buffer_size(_socketSendBufferSize), err);
		if (err && err != boost::system::errc::not_supported)
		{
			TC_LOG_ERROR(LOG_FILTER_GENERAL, "ToolSocketMgr::OnSocketOpen sock.set_option(boost::asio::socket_base::send_buffer_size) err = %s", err.message().c_str());
			return;
		}
	}

	// Set TCP_NODELAY.
	if (_tcpNoDelay)
	{
		boost::system::error_code err;
		sock.set_option(boost::asio::ip::tcp::no_delay(true), err);
		if (err)
		{
			TC_LOG_ERROR(LOG_FILTER_GENERAL, "ToolSocketMgr::OnSocketOpen sock.set_option(boost::asio::ip::tcp::no_delay) err = %s", err.message().c_str());
			return;
		}
	}

	//sock->m_OutBufferSize = static_cast<size_t> (m_SockOutUBuff);

	BaseSocketMgr::OnSocketOpen(std::forward<tcp::socket>(sock), threadIndex);
}

NetworkThread<ToolSocket>* ToolSocketMgr::CreateThreads() const
{
	return new NetworkThread<ToolSocket>[GetNetworkThreadCount()];
}
