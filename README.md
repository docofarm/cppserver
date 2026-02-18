# C++를 사용한 이미지를 보여주는 서버를 연습했습니다.

## 사용 기술
    C++

    Winsock2

    TCP Socket

    HTTP/1.1 프로토콜

    바이너리 파일 처리


- 2/13
    - mutex를 사용했음.(Mutual Exclusion(상호배제)) race condition을 방지할 때 사용하는 도구. 멀티스레딩을 할 때 한번에 한 번만 들어갈 수 있게 조절하는 기능을 가진다고 함.
    - lock_guard. 자동 잠금 장치

- 2/14
    - header 파싱 부분 공부가 더 필요. istringstream, npos, substr 등
    - 간단한 path 기반 라우팅 추가 

- 2/16
    - body 파싱, 간단한 로그인 기능 추가
    - 정해진 id, pw를 POST 했을때 성공 200, 실패 401

- 2/17
    - handleClient 함수에서 route 부분 분리
- 2/19
    - HttpRequest 리팩토링

## 컴파일
    g++ imageserver.cpp -o server.exe -lws2_32



### Educational HTTP server implementation using Winsock.
### This server is for educational purposes only.
### It does not implement security features, input validation, or production-level error handling.