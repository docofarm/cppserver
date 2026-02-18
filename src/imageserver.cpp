#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>
#include <sstream>
#include <map>
#include <functional>

#pragma comment(lib, "ws2_32.lib")

using Handler = std::function<std::string(const std::string&)>;

std::map<std::string, Handler> router;

std::mutex mtx;
int clientCount = 0;

std::string makeResponse(
    const std::string& status,
    const std::string& contentType,
    const std::string& body
){
    return 
        "HTTP/1.1 "+ status + "\r\n"
        "Content-Type: " + contentType + "\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + 
        body;
}

std::string routeRequest(
    const std::string& method,
    const std::string& path,
    const std::string& body
)
{
    std::string key = method + " " + path;

    if(router.find(key) != router.end())
    {
        return router[key](body);
    }

    if(method == "GET" || method == "POST")
    {
        return makeResponse("404 not found", "text/plain", "Not found");
    }

    return makeResponse("405 Method not Allowed", "text/plain", "");
}

void handleClient(SOCKET clientSock){

    {
        std::lock_guard<std::mutex> lock(mtx);
        clientCount++;
        std::cout << "Client Connect | Current Client: " << clientCount << "\n";
    }

    char buffer[2048];

    int received = recv(clientSock, buffer, sizeof(buffer) -1, 0);

    if(received > 0)
    {
        buffer[received] = '\0';

        std::string request(buffer);

        //1. request line parshing
        std::istringstream requestStream(request);
        std::string requestLine;

        std::getline(requestStream, requestLine);

        // remove \r
        if(!requestLine.empty() && requestLine.back() == '\r')
        {
            requestLine.pop_back();
        }
        std::istringstream lineStream(requestLine);

        std::string method;
        std::string path;
        std::string version;

        lineStream >> method >> path >> version;

        std::cout << "Method: " << method << "\n";
        std::cout << "Path: " << path << "\n";
        std::cout << "Version: " << version << "\n";

        //2. header parshing
        std::map<std::string, std::string> headers;
        std::string headerLine;

        while (std::getline(requestStream, headerLine)){
            if(headerLine == "\r" || headerLine.empty()){
                break;
            }

            if(headerLine.back() == '\r')
            {
                headerLine.pop_back();
            }

            size_t colonPos = headerLine.find(":");
            if(colonPos != std::string::npos)
            {
                std::string key = headerLine.substr(0, colonPos);
                std::string value = headerLine.substr(colonPos + 1);

                if(!value.empty() && value[0] == ' '){
                    value.erase(0,1);
                }
                headers[key] = value;
            }
        }

        if(headers.find("Host") != headers.end()){
            std::cout << "Host Header: " << headers["Host"] << "\n";
        }

        std::string body;
        std::string response;

        //3. body parsing
        if(headers.find("Content-Length") != headers.end())
        {
            int contentLength = std::stoi(headers["Content-Length"]);
            body.resize(contentLength);
            requestStream.read(&body[0], contentLength);
        }

        if(!body.empty()){
            std::cout << "Body: "<< body << "\n";
        }

        response = routeRequest(method, path, body);

        send(clientSock, response.c_str(), response.size(), 0);
    }

    closesocket(clientSock);

    {
        std::lock_guard<std::mutex> lock(mtx);
        clientCount--;
        std::cout << "Client Disconnected | Current Client: " << clientCount << "\n";
    }
}

int main(){
    router["GET /"] = [](const std::string& body){
        return makeResponse("200 OK", "text/plain", "Welcome my Server");
    };
    router["GET /first"] = [](const std::string& body){
        return makeResponse("200 OK", "text/plain", "First Page");
    };
    router["POST /login"] = [](const std::string& body){
        std::string id;
        std::string pw;

        size_t idPos = body.find("id=");
        size_t pwPos = body.find("pw=");

        if(idPos != std::string::npos && pwPos != std::string::npos){
            size_t idEnd = body.find("&", idPos);
            id = body.substr(idPos + 3, idEnd - (idPos + 3));
            pw = body.substr(pwPos + 3);
        }

        if(id == "admin" && pw=="1234")
        {
            return makeResponse("200 Ok", "application/json", "{\"status\":\"login success\",\"message\":\"login success\"}");
        }
        else
        {
            return makeResponse("401 unauthorized", "application/json", "{\"status\":\"login failed\",\"message\":\"login failed\"}");
        }
    };

    WSADATA wsaData;

    if(WSAStartup(MAKEWORD(2,2), &wsaData) != 0)
    {
        std::cout << "WSAStarup failed.\n";
        return 1;
    }

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    if(listenSock == INVALID_SOCKET)
    {
        std::cout << "Socket creation failed.\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if(bind(listenSock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cout << "Bind failed.\n";
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    if(listen(listenSock, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cout << "Listen failed.\n";
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    std::cout << "Server listening on port 8080\n";

    while(true)
    {
        SOCKET clientSock = accept(listenSock, nullptr, nullptr);

        if(clientSock == INVALID_SOCKET)
            continue;
        
        std::thread t(handleClient, clientSock);
        t.detach();
    }

    closesocket(listenSock);
    WSACleanup();

    return 0;
}