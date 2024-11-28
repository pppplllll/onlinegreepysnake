#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/bind/bind.hpp>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace beast = boost::beast;
namespace websocket = boost::beast::websocket;
namespace asio = boost::asio;

// 数据结构
struct Snake {
    std::vector<std::pair<int, int>> body;  // 蛇的身体
    int direction;                         // 当前方向（0上, 1右, 2下, 3左）
    bool alive;                            // 蛇是否存活
};

struct GameRoom {
    std::unordered_map<std::string, Snake> players; // 玩家列表
    std::pair<int, int> food;                       // 食物位置
    std::mutex roomMutex;                           // 线程安全
};

// 全局房间列表
std::unordered_map<std::string, GameRoom> rooms;

// 游戏逻辑更新
void updateGameRoom(GameRoom &room) {
    std::lock_guard<std::mutex> lock(room.roomMutex);

    for (auto &[id, snake] : room.players) {
        if (!snake.alive) continue;

        // 更新蛇的位置
        auto head = snake.body.front();
        switch (snake.direction) {
            case 0: head.second--; break; // 上
            case 1: head.first++; break; // 右
            case 2: head.second++; break; // 下
            case 3: head.first--; break; // 左
        }

        // 碰撞检测
        if (head == room.food) {
            // 吃到食物，增加长度，生成新食物
            snake.body.insert(snake.body.begin(), head);
            room.food = {rand() % 20, rand() % 20};  // 随机生成新的食物位置
        } else {
            // 移动蛇
            snake.body.insert(snake.body.begin(), head);
            snake.body.pop_back();
        }
    }
}

// 处理 WebSocket 消息（接收方向控制命令）
void handleWebSocketMessage(websocket::stream<asio::ip::tcp::socket> &ws, std::string message, GameRoom &room) {
    std::lock_guard<std::mutex> lock(room.roomMutex);

    // 假设消息格式是方向（例如"UP", "RIGHT"）
    for (auto &[id, snake] : room.players) {
        if (message == "UP") {
            snake.direction = 0;  // 上
        } else if (message == "RIGHT") {
            snake.direction = 1;  // 右
        } else if (message == "DOWN") {
            snake.direction = 2;  // 下
        } else if (message == "LEFT") {
            snake.direction = 3;  // 左
        }
    }
}

// WebSocket 会话处理
void doSession(asio::ip::tcp::socket socket) {
    try {
        // 将 TCP 套接字升级为 WebSocket 套接字
        websocket::stream<asio::ip::tcp::socket> ws(std::move(socket));
        ws.accept();

        // 创建或加入游戏房间
        std::string room_id = "game_room_1";  // 假设玩家加入房间1
        GameRoom &room = rooms[room_id];

        // 玩家加入房间
        std::string player_id = "player_1";  // 假设玩家ID为player_1
        Snake snake = {{std::make_pair(5, 5)}, 1, true};  // 初始位置为(5, 5)，向右
        room.players[player_id] = snake;

        // 启动游戏逻辑更新线程
        std::thread([&]() {
            while (true) {
                updateGameRoom(room);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));  // 每100ms更新一次
            }
        }).detach();

        // 接收客户端消息并处理
        for (;;) {
            beast::flat_buffer buffer;
            ws.read(buffer);

            std::string message = beast::buffers_to_string(buffer.data());
            handleWebSocketMessage(ws, message, room);
        }
    } catch (const std::exception &e) {
        std::cerr << "Error in session: " << e.what() << std::endl;
    }
}

// 启动服务器
void runServer() {
    try {
        asio::io_context io_context;

        // 监听端口9001
        asio::ip::tcp::acceptor acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 9001));

        for (;;) {
            // 接受客户端连接
            asio::ip::tcp::socket socket(io_context);
            acceptor.accept(socket);

            // 为每个客户端启动一个独立的线程
            std::thread(&doSession, std::move(socket)).detach();
        }
    } catch (const std::exception &e) {
        std::cerr << "Error in server: " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "Server started on port 9001" << std::endl;
    runServer();
    return 0;
}

