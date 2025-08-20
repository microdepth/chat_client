#include <iostream>
#include <winsock2.h>
#include <windows.h>
#include <chrono>
#include <atomic>
#include <thread>

using namespace std;

class TeeBuf : public streambuf {
public:
    TeeBuf(streambuf* sb1, streambuf* sb2) : buf1(sb1), buf2(sb2) {
        if (!buf1 || !buf2) {
            throw std::invalid_argument("streambuf pointers must not be null");
        }
    };

    int overflow(int c) override {
        if (c == EOF) {
            return EOF;
        } else {
            const char ch = static_cast<char>(c);
            if (buf1->sputc(ch) == EOF || buf2->sputc(ch) == EOF) {
                return EOF;
            }
            return c;
        }
    }

    int sync() override {
        if (buf1->pubsync() == -1 || buf2->pubsync() == -1) {
            return -1;
        }
        return 0;
    }

private:
    streambuf* buf1;
    streambuf* buf2;
};

string encryption_key = "monkey";
atomic<bool> client_id_received{false};

string xor_text_encryption(string& data, const string& key) {
    for (size_t i = 0; i < data.size(); i++) {
        data[i] ^= key[i % key.size()];
    }
    return data;
}
void log(const string& level, const string& msg) {
    auto current_time = time(nullptr);
    auto local_time = *localtime(&current_time);
    cout << put_time(&local_time, "[%Y-%m-%d %H:%M:%S] ") << "[" << level << "] " << msg << endl;
}
DWORD WINAPI MessageReciever(LPVOID lp_param) {
    SOCKET client_socket = (SOCKET)lp_param;

    char recv_buffer[512];
    int bytes_recieved;

    const int retry_interval = 10;
    const int max_retries = 5;
    int retry_count = 0;

    while (true) {
        bytes_recieved = recv(client_socket, recv_buffer, sizeof(recv_buffer), 0);
        if (bytes_recieved > 0) {
            if (bytes_recieved < sizeof(recv_buffer)) {
                recv_buffer[bytes_recieved] = '\0';
            } else {
                recv_buffer[sizeof(recv_buffer) - 1] = '\0';
            }
            string recieved_message(recv_buffer);

            if (recieved_message.find("you are client number") != std::string::npos) {
                client_id_received = true;
            }

            log("MESSAGE", recieved_message);
        } else if (bytes_recieved == 0) {
            log("INFO", "disconnected");
            break;
        } else {
            if (retry_count >= max_retries) {
                string message3 = "max retries reached " + to_string(WSAGetLastError());
                log("ERROR", message3);
                break;
            }
            string message = "recv failed, error: " + to_string(WSAGetLastError());
            log("ERROR", message);
            this_thread::sleep_for(std::chrono::milliseconds(retry_interval));
            retry_count++;
            continue;
        }
    }

    if (client_socket != INVALID_SOCKET) {
        closesocket(client_socket);
        client_socket = INVALID_SOCKET;
    }
    return 0;
}

int main() {
    WSAData wsa_data{};

    int init_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (init_result != 0) {
        string message = "wsastartup failed, error: " + to_string(WSAGetLastError());
        log("ERROR", message);
        return 1;
    }

    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == INVALID_SOCKET) {
        string message = "socket failed, error: " + to_string(WSAGetLastError());
        log("ERROR", message);
        WSACleanup();
        return 1;
    }

    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_address.sin_port = htons(8080);

    int connection_result;
    const int retry_interval = 1000;
    const int max_retries = 5;
    int retry_count = 0;

    do {
        connection_result = connect(client_socket, (sockaddr*) &server_address, sizeof(server_address));
        if (connection_result == SOCKET_ERROR) {
            retry_count++;
            string message = "connect failed, error: " + to_string(WSAGetLastError());
            log("ERROR", message);
            if (retry_count >= max_retries) {
                log("ERROR", "maximum retries reached");
                if (closesocket(client_socket) == SOCKET_ERROR) {
                    log("ERROR", "closesocket in dowhile failed, error: " + to_string(WSAGetLastError()));
                }
                if (WSACleanup() == SOCKET_ERROR) {
                    log("ERROR", "wsacleanup in dowhile failed, error: " + to_string(WSAGetLastError()));
                }
                return 1;
            }
            log("INFO", "waiting for " + to_string(retry_interval) + " ms");
            this_thread::sleep_for(std::chrono::milliseconds(retry_interval));
        }
    } while (connection_result == SOCKET_ERROR);

    HANDLE thread_handle = CreateThread(nullptr, 0, MessageReciever, (LPVOID) client_socket, 0, nullptr);

    if (thread_handle == nullptr) {
        string message = "createthread failed, error: " + to_string(GetLastError());
        log("ERROR", message);
    } else {
        CloseHandle(thread_handle);
    }

    char send_buffer[512];

    while (true) {
        while (!client_id_received) {
            this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        log("PROMPT", "send: ");
        string message;
        getline(cin, message);

        if (message.length() >= sizeof(send_buffer)) {
            message.resize(sizeof(send_buffer) - 1);
        }

        strncpy(send_buffer, message.c_str(), sizeof(send_buffer));
        send_buffer[sizeof(send_buffer) - 1] = '\0';

        int bytes_sent = send(client_socket, send_buffer, strlen(send_buffer), 0);
        if (bytes_sent == SOCKET_ERROR) {
            log("ERROR", "send failed, error: " + to_string(WSAGetLastError()));
            break;
        }

        if (string(send_buffer) == "disconnect") {
            shutdown(client_socket, SD_SEND);
            break;
        }

        memset(send_buffer, 0, sizeof(send_buffer));
    }

    closesocket(client_socket);
    WSACleanup();
    return 0;
}