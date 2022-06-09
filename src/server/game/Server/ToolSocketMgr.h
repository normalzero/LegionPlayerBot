
#ifndef __TOOLSOCKETMGR_H
#define __TOOLSOCKETMGR_H

class ToolSocket;

#include "SocketMgr.h"

/// Manages all sockets connected to peers and network threads
class ToolSocketMgr : public SocketMgr<ToolSocket>
{
	typedef SocketMgr<ToolSocket> BaseSocketMgr;

public:
	static ToolSocketMgr& Instance();

	/// Start network, listen at address:port .
	bool StartNetwork(boost::asio::io_service& service, std::string const& bindIp, uint16 port, int networkThreads) override;

	/// Stops all network threads, It will wait for all running threads .
	void StopNetwork() override;

	void OnSocketOpen(tcp::socket&& sock, uint32 threadIndex) override;

protected:
	ToolSocketMgr();

	NetworkThread<ToolSocket>* CreateThreads() const override;

private:
	int32 _socketSendBufferSize;
	int32 m_SockOutUBuff;
	bool _tcpNoDelay;
};

#define sToolSocketMgr ToolSocketMgr::Instance()

#endif // __TOOLSOCKETMGR_H
