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
#include <unordered_map>
#include <random>
#include <chrono>
#include <filesystem>

#pragma comment(lib, "ws2_32.lib")

struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse{
    std::string version = "HTTP/1.1";
    int statusCode = 200;
    std::string statusMessage = "OK";
    std::map<std::string, std::string> headers;
    std::string body;
};

struct Session
{
    std::string userId;
    std::chrono::steady_clock::time_point expireTime;
};

std::mutex mtx;
int clientCount = 0;

std::string generateSessionId()
{
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<int> dist(0, 15);

    std::string sessionId;
    const char* hex = "0123456789abcdef";

    for(int i = 0; i < 32; i++)
    {
        sessionId += hex[dist(rng)];
    }

    return sessionId;
}

class SessionManager
{
private:
    std::unordered_map<std::string, Session> sessions;
    std::mutex sessionMutex;
public:
    std::string createSession(const std::string& userId)
    {
        std::lock_guard<std::mutex> lock(sessionMutex);

        std::string sessionId = generateSessionId();

        Session session;
        session.userId = userId;
        session.expireTime = std::chrono::steady_clock::now() + std::chrono::minutes(1);

        sessions[sessionId] = session;

        return sessionId;
    }

    bool isValid(const std::string& sessionId)
    {
        std::lock_guard<std::mutex> lock(sessionMutex);

        auto it = sessions.find(sessionId);

        if(it == sessions.end())
        {
            return false;
        }
        if(std::chrono::steady_clock::now() > it -> second.expireTime)
        {
            sessions.erase(it);
            return false;
        }

        return true;
    }

    void remove(const std::string& sessionId)
    {
        std::lock_guard<std::mutex> lock(sessionMutex);
        sessions.erase(sessionId);
    }

    void cleanExpired()
    {
        std::lock_guard<std::mutex> lock(sessionMutex);

        auto now = std::chrono::steady_clock::now();

        for(auto it = sessions.begin(); it !=sessions.end();)
        {
            if(now > it-> second.expireTime)
            {
                std::cout << "Expired session removed\n";
                it = sessions.erase(it);
            }
            else{
                ++it;
            }
        }
    }
};

SessionManager sessionManager;

std::string readFile(const std::string& path, bool& success)
{
    std::ifstream file(path, std::ios::binary);
    if(!file.is_open())
    {
        success = false;
        return "";
    }

    std::ostringstream ss;
    ss << file.rdbuf();

    success = true;
    return ss.str();
};

std::string getContentType(const std::string& path)
{
    if(path.find(".html") != std::string::npos) return "text/html";
    if(path.find(".css") != std::string::npos) return "text/css";
    if(path.find(".js") != std::string::npos) return "application/javascript";
    if(path.find(".png") != std::string::npos) return "image/png";
    if(path.find(".jpg") != std::string::npos || path.find(".jpeg") != std::string::npos) return "image/jpeg";
    if(path.find(".gif") != std::string::npos) return "image/gif";
    
    return "text/plain";
};

std::string getSessionIdFromCookie(const HttpRequest& req)
{
    auto it = req.headers.find("Cookie");
    if(it == req.headers.end())
    {
        return "";
    }
    std::string cookie = it->second;

    size_t pos = cookie.find("SESSIONID=");
    if(pos == std::string::npos)
    {
        return "";
    }
    return cookie.substr(pos + 10);
};

using Handler = std::function<HttpResponse(const HttpRequest&)>;
using Middleware = std::function<Handler(Handler)>;

Handler chain(std::vector<Middleware> middlewares, Handler finalHandler)
{
    Handler current = finalHandler;

    for(auto it= middlewares.rbegin(); it != middlewares.rend(); ++it)
    {
        current = (*it)(current);
    }

    return current;
};

Middleware requireAuth = [](Handler next)
{
    return [next](const HttpRequest& req) -> HttpResponse
    {
        std::string sessionId = getSessionIdFromCookie(req);

        if(!sessionManager.isValid(sessionId))
        {
            HttpResponse res;
            res.statusCode = 401;
            res.statusMessage = "Unauthorized";
            res.body = "Login required or session expired";
            res.headers["Content-Type"] = "text/plain";
            res.headers["Content-Length"] = std::to_string(res.body.size());
            return res;
        }
        return next(req);
    };
};

Middleware logging = [](Handler next)
{
    return [next](const HttpRequest& req) -> HttpResponse
    {
        std::cout << "[LOG]" << req.method << " " << req.path << "\n";
        return next(req);
    };
};

class Router
{
private:
    std::map<std::string, Handler> routes;

public:
    void addRoute(const std::string& method, const std::string& path, Handler handler)
    {
        std::string key = method + " " + path;
        routes[key] = handler;
    }

    HttpResponse route(const HttpRequest& req)
    {
        std::string key = req.method + " " + req.path;

        //라우트 먼저 검사
        if(routes.find(key) != routes.end())
        {
            return routes[key](req);
        }

        //Get 요청 static 시도
        if(req.method == "GET")
        {
            std::string filePath;

            if(req.path.find("/uploads/") == 0)
            {
                filePath = req.path.substr(1);
            }
            else
            {
                filePath = "static" + req.path;
            }

            bool ok = false;
            std::string content = readFile(filePath, ok);

            if(ok)
            {
                HttpResponse res;
                res.body = content;
                res.headers["Content-Type"] = getContentType(filePath);
                res.headers["Content-Length"] = std::to_string(res.body.size());
                return res;
            }
        }

        //다 없으면 404
        HttpResponse res;
        res.statusCode = 404;
        res.statusMessage = "Not found";
        res.body = "404 Not Found";
        res.headers["Content-Type"] = "text/plain";
        res.headers["Content-Length"] = std::to_string(res.body.size());

        return res;
    }
};

Router router;


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

HttpRequest parseHttpRequest(const std::string& raw)
{
    HttpRequest req;

    std::istringstream requestStream(raw);
    std::string requestLine;

    //1. request Line
    std::getline(requestStream, requestLine);

    if(!requestLine.empty() && requestLine.back() == '\r')
        requestLine.pop_back();

    std::istringstream lineStream(requestLine);
    lineStream >> req.method >> req.path >> req.version;

    //2. Header
    std::string headerLine;

    while(std::getline(requestStream, headerLine))
    {
        if(headerLine == "\r" || headerLine.empty())
        {
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

            if(!value.empty() && value[0] == ' ')
            {
                value.erase(0,1);
            }

            req.headers[key] = value;
        }
    }

    //3. body
    if(req.headers.find("Content-Length") != req.headers.end())
    {
        int contentLength = std::stoi(req.headers["Content-Length"]);
        req.body.resize(contentLength);
        requestStream.read(&req.body[0], contentLength);
    }

    return req;
}

//패킷 생성기
std::string serializeResponse(const HttpResponse& res)
{
    std::ostringstream responseStream;

    responseStream << res.version << " "
                    << res.statusCode << " "
                    << res.statusMessage << "\r\n";

    for(const auto& header : res.headers)
    {
        responseStream << header.first << ": "
                        << header.second << "\r\n";
    }

    responseStream << "\r\n";
    responseStream << res.body;

    return responseStream.str();
}

std::string extractBoundary(const std::string& contentType)
{
    std::string key = "boundary=";
    size_t pos = contentType.find(key);

    if(pos == std::string::npos)
    {
        return "";
    }

    return "--" + contentType.substr(pos + key.size());
}

std::string extractFilename(const std::string& header)
{
    std::string key = "filename=\"";
    size_t pos = header.find(key);

    if(pos == std::string::npos)
    {
        return "";
    }

    size_t start = pos + key.size();
    size_t end = header.find("\"", start);

    return header.substr(start, end - start);
}

std::string readHttpRequest(SOCKET sock)
{
    std::string request;
    char buffer[4096];
    int received;

    int contentLength = 0;
    bool headerParsed = false;

    while(true)
    {
        received = recv(sock, buffer, sizeof(buffer), 0);

        if(received <= 0)
        {
            break;
        }

        request.append(buffer, received);

        if(!headerParsed)
        {
            size_t headerEnd = request.find("\r\n\r\n");

            if(headerEnd != std::string::npos)
            {
                headerParsed = true;

                size_t pos = request.find("Content-Length");
                if(pos != std::string::npos)
                {
                    contentLength = std::stoi(request.substr(pos + 15));
                }

                size_t totalNeeded = headerEnd + 4 + contentLength;

                if(request.size() >= totalNeeded)
                {
                    break;
                }
            }
        }
        else{
            size_t headerEnd = request.find("\r\n\r\n");
            size_t totalNeeed = headerEnd + 4 + contentLength;

            if(request.size() >= totalNeeed)
            {
                break;
            }
        }
    }

    return request;
}

bool saveFile(const std::string& filename, const std::string& data)
{
    std::string path = "uploads/" + filename;

    std::ofstream file(path, std::ios::binary);

    if(!file.is_open())
    {
        return false;
    }

    file.write(data.c_str(), data.size());
    file.close();

    return true;
}

bool isImageFile(const std::string& filename)
{
    if(filename.find(".png") != std::string::npos) return true;
    if(filename.find(".jpg") != std::string::npos) return true;  
    if(filename.find(".jpeg") != std::string::npos) return true; 
    if(filename.find(".gif") != std::string::npos) return true; 

    return false;
}

bool parseMultipartImage(const HttpRequest& req, std::string& filename, std::string& fileData)
{
    auto it = req.headers.find("Content-Type");

    if(it == req.headers.end())
    {
        return false;
    }

    std::string boundary = extractBoundary(it -> second);

    size_t headerEnd = req.body.find("\r\n\r\n");

    if(headerEnd == std::string::npos)
    {
        return false;
    }

    std::string header = req.body.substr(0, headerEnd);

    filename = extractFilename(header);

    if(filename.empty())
    {
        return false;
    }

    size_t dataStart = headerEnd + 4;
    size_t dataEnd = req.body.find(boundary, dataStart);

    if(dataEnd == std::string::npos)
    {
        return false;
    }

    fileData = req.body.substr(dataStart, dataEnd - dataStart - 2);

    return true;
}

void handleClient(SOCKET clientSock){

    {
        std::lock_guard<std::mutex> lock(mtx);
        clientCount++;
        std::cout << "Client Connect | Current Client: " << clientCount << "\n";
    }

    std::string rawRequest = readHttpRequest(clientSock);

    if(!rawRequest.empty())
    {

        HttpRequest req = parseHttpRequest(rawRequest);

        std::cout << "Method: " << req.method << "\n";
        std::cout << "Path: " << req.path << "\n";
        std::cout << "Version: " << req.version << "\n";

        if(req.headers.find("Host") != req.headers.end())
        {
            std::cout << "Host Header: " << req.headers["Host"] << "\n";
        }
        if(!req.body.empty())
        {
            std::cout << "Body Length: " << req.body.size() << "\n";
        }

        HttpResponse res = router.route(req);

        std::string responseStr = serializeResponse(res);
        std::cout << "Body Length: " << req.body.size() << "\n";
        send(clientSock, responseStr.c_str(), responseStr.size(), 0);

        std::cout << "RAW SIZE: " << rawRequest.size() << "\n";
        std::cout << "BODY SIZE: " << req.body.size() << "\n";
    }

    closesocket(clientSock);

    {
        std::lock_guard<std::mutex> lock(mtx);
        clientCount--;
        std::cout << "Client Disconnected | Current Client: " << clientCount << "\n";
    }
}

void sessionClear()
{
    while(true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        sessionManager.cleanExpired();
    }
}

void setupRoutes()
{
    router.addRoute("GET", "/", [](const HttpRequest& req)
    {
        bool ok = false;
        std::string content = readFile("static/index.html", ok);
        HttpResponse res;

        if(content.empty())
        {
            res.statusCode = 404;
            res.statusMessage = "Not Found";
            res.body = "File not Found";
            res.headers["Content-Type"] = "text/plain";
        }
        else
        {
            res.body = content;
            res.headers["Content-Type"] = "text/html";
        }

        res.headers["Content-Length"] = std::to_string(res.body.size());
        return res;
    });

    router.addRoute("GET", "/first", chain({logging, requireAuth}, [](const HttpRequest& req){  
        HttpResponse res;
        res.body = "First Page You Login Success";
        res.headers["Content-Type"] = "text/plain";
        res.headers["Content-Length"] = std::to_string(res.body.size());
        return res;
    }));
    

    router.addRoute("POST", "/login" ,[](const HttpRequest& req)
    {
        HttpResponse res;

        std::string id;
        std::string pw;

        size_t idPos = req.body.find("id=");
        size_t pwPos = req.body.find("pw=");

        if(idPos != std::string::npos && pwPos != std::string::npos)
        {
            size_t idEnd = req.body.find("&", idPos);

            if(idEnd == std::string::npos)
                id = req.body.substr(idPos + 3);
            else
                id = req.body.substr(idPos + 3, idEnd - (idPos + 3));

            pw = req.body.substr(pwPos + 3);
        }

        if(id == "admin" && pw=="1234")
        {
            res.statusCode = 200;
            res.statusMessage = "OK";
            res.body = "{\"status\":\"login success\",\"message\":\"login success\"}";

            std::string sessionId = sessionManager.createSession(id);
            res.headers["Set-Cookie"] = "SESSIONID=" + sessionId + "; Path=/; HttpOnly";
        }
        else
        {
            res.statusCode = 401;
            res.statusMessage = "Unauthorized";
            res.body = "{\"status\":\"login failed\",\"message\":\"login failed\"}";
        }

        res.headers["Content-Type"] = "application/json";
        res.headers["Content-Length"] = std::to_string(res.body.size());

        return res;
    });

    router.addRoute("GET", "/logout" ,[](const HttpRequest& req)
    {
        HttpResponse res;

        std::string sessionId = getSessionIdFromCookie(req);
        sessionManager.remove(sessionId);

        res.headers["Set-Cookie"] = "SESSIONID=; Path=/; HttpOnly; Max-Age=0";

        res.statusCode = 302;
        res.statusMessage = "Found";
        res.headers["Location"] = "/login.html";
        res.body = "";
        res.headers["Content-Length"] = "0";

        return res;
    });

    router.addRoute("POST", "/uploads", [](const HttpRequest& req)
    {
        HttpResponse res;

        std::string filename;
        std::string fileData;

        if(!parseMultipartImage(req, filename, fileData))
        {
            res.statusCode = 400;
            res.statusMessage = "Bad Request";
            res.body = "Upload parse Error";
        }else if(!isImageFile(filename))
        {   
            res.statusCode = 400;
            res.statusMessage = "Bad Request";
            res.body = "Only image allowed";
        }else
        {
            if(saveFile(filename, fileData))
            {
                res.body = "Upload Success";
            }else
            {
                res.statusCode = 500;
                res.body = "Save failed";
            }
        }
        res.headers["Content-Type"] = "text/plain";
        res.headers["Content-Length"] = std::to_string(res.body.size());

        return res;
    });

    router.addRoute("GET", "/gallery", [](const HttpRequest& req){
        HttpResponse res;

        std::string html = "<html><body>";
        html += "<h1>Image Gallery</h1>";

        for(const auto& entry : std::filesystem::directory_iterator("uploads"))
        {
            std::string filename = entry.path().filename().string();

            html += "<div>";
            html += "<img src=\"/uploads/" + filename + "\" width=\"200\">";
            html += "</div>";
        }

        html += "</body></html>";

        res.body = html;
        res.headers["Content-Type"] = "text/html";
        res.headers["Content-Length"] = std::to_string(res.body.size());

        return res;
    });
}

int main(){
    setupRoutes();
    
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

    std::thread cleaner(sessionClear);
    cleaner.detach();

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