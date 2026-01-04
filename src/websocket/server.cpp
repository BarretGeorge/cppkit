#include "cppkit/websocket/server.hpp"
#include "cppkit/http/server/http_request.hpp"
#include "cppkit/log/log.hpp"
#include "cppkit/crypto/base.hpp"
#include "cppkit/crypto/sha1.hpp"
#include <algorithm>
#include <sstream>

namespace cppkit::websocket
{
    WebSocketServer::WebSocketServer(const std::string& host, const uint16_t port)
        : _tcpServer(&_loop, host, port), _host(host), _port(port)
    {
    }

    WebSocketServer::~WebSocketServer() = default;

    void WebSocketServer::setOnConnect(OnConnectHandler handler)
    {
        _onConnect = std::move(handler);
    }

    void WebSocketServer::setOnMessage(OnMessageHandler handler)
    {
        _onMessage = std::move(handler);
    }

    void WebSocketServer::setOnClose(OnCloseHandler handler)
    {
        _onClose = std::move(handler);
    }

    void WebSocketServer::start()
    {
        // 设置回调函数
        _tcpServer.setOnConnection([this](const event::ConnInfo& connInfo)
        {
            this->onTcpConnect(connInfo);
        });

        _tcpServer.setOnMessage([this](const event::ConnInfo& connInfo, const std::vector<uint8_t>& data)
        {
            this->onTcpMessage(connInfo, data);
        });

        _tcpServer.setOnClose([this](const event::ConnInfo& connInfo)
        {
            if (const std::string clientId = connInfo.getClientId(); _connStates.contains(clientId))
            {
                _connStates.erase(clientId);
            }

            if (_onClose)
            {
                const ConnInfo wsConnInfo(connInfo);
                _onClose(wsConnInfo);
            }
        });

        _tcpServer.start();
        std::cout << "Started ws server on " << this->_host << ":" << this->_port << std::endl;
        _loop.run();
    }

    void WebSocketServer::stop()
    {
        _loop.stop();
        _tcpServer.stop();
    }

    std::string WebSocketServer::getHost() const
    {
        return _host;
    }

    int WebSocketServer::getPort() const
    {
        return _port;
    }

    void WebSocketServer::onTcpConnect(const event::ConnInfo& connInfo)
    {
        // 初始状态为握手中
        const std::string clientId = connInfo.getClientId();
        _connStates[clientId] = ConnData{ConnState::HAND_SHAKING, {}};
    }

    void WebSocketServer::onTcpMessage(const event::ConnInfo& connInfo, const std::vector<uint8_t>& data)
    {
        const std::string clientId = connInfo.getClientId();
        const auto it = _connStates.find(clientId);

        if (it == _connStates.end())
        {
            return; // 找不到连接状态，忽略消息
        }

        if (auto& [state, buffer] = it->second; state == ConnState::HAND_SHAKING)
        {
            // 收集数据直到找到完整的HTTP请求头
            buffer.insert(buffer.end(), data.begin(), data.end());

            // 检查是否收到完整的HTTP头（以\r\n\r\n结尾）
            std::string bufferStr(buffer.begin(), buffer.end());

            if (const size_t headerEnd = bufferStr.find("\r\n\r\n"); headerEnd != std::string::npos)
            {
                // 完成握手
                if (handleHandshake(connInfo, buffer))
                {
                    state = ConnState::CONNECTED;

                    // 通知连接已建立
                    if (_onConnect)
                    {
                        const auto req =
                            http::server::HttpRequest::parse(connInfo.getFd(),
                                                             std::string(buffer.begin(), buffer.end()), "");
                        _onConnect(req, ConnInfo(connInfo));
                    }

                    buffer.clear();
                }
            }
        }
        else if (state == ConnState::CONNECTED) // 已连接状态
        {
            // 追加数据到缓冲区
            buffer.insert(buffer.end(), data.begin(), data.end());

            Frame frame;
            size_t parsedOffset = 0;

            while (true)
            {
                if (buffer.size() < parsedOffset + 2)
                {
                    break; // 不完整的帧头
                }

                // 尝试解析帧
                if (const size_t parsedBytes = parseFrame(std::vector(buffer.begin() + parsedOffset, buffer.end()),
                                                          frame);
                    parsedBytes > 0)
                {
                    parsedOffset += parsedBytes;

                    // 处理消息
                    if (_onMessage)
                    {
                        if (frame.opCode == MessageType::CLOSE)
                        {
                            // 关闭连接
                            connInfo.close();
                            return;
                        }
                        ConnInfo wsConnInfo(connInfo);
                        _onMessage(wsConnInfo, frame.payload, frame.opCode);
                    }
                }
                else
                {
                    break; // 无法解析完整帧，等待更多数据
                }
            }

            // 移除已解析的数据
            if (parsedOffset > 0)
            {
                if (parsedOffset < buffer.size())
                {
                    buffer = std::vector(buffer.begin() + static_cast<long>(parsedOffset), buffer.end());
                }
                else
                {
                    buffer.clear();
                }
            }
        }
    }

    bool WebSocketServer::handleHandshake(const event::ConnInfo& connInfo, const std::vector<uint8_t>& data)
    {
        std::string request(reinterpret_cast<const char*>(data.data()), data.size());

        // Find Sec-WebSocket-Key
        std::string key;
        size_t keyPos = request.find("Sec-WebSocket-Key: ");
        if (keyPos == std::string::npos)
        {
            return false;
        }

        size_t keyEnd = request.find("\r\n", keyPos);
        if (keyEnd == std::string::npos)
        {
            return false;
        }

        key = request.substr(keyPos + 19, keyEnd - (keyPos + 19));

        // Generate Sec-WebSocket-Accept
        const std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        std::string combined = key + magic;
        const auto sha1Binary = crypto::SHA1::shaBinary(combined);
        std::vector sha1Bytes(sha1Binary.begin(), sha1Binary.end());
        std::string accept = crypto::Base64::encode(sha1Bytes);

        // Build response
        std::stringstream response;
        response << "HTTP/1.1 101 Switching Protocols\r\n";
        response << "Upgrade: websocket\r\n";
        response << "Connection: Upgrade\r\n";
        response << "Sec-WebSocket-Accept: " << accept << "\r\n\r\n";

        std::string respStr = response.str();
        ssize_t sent = connInfo.send(reinterpret_cast<const uint8_t*>(respStr.c_str()), respStr.size());

        return sent == static_cast<ssize_t>(respStr.size());
    }

    // void WSServer::onTcpClose(const event::ConnInfo& connInfo)
    // {
    // }
}
