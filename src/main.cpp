#ifdef WIN32
#include <sdkddkver.h>
#endif

#include "seabattle.hpp"

#include <atomic>
#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <string_view>

namespace net = boost::asio;
using net::ip::tcp;
using namespace std::literals;

void PrintFieldPair(const SeabattleField& left, const SeabattleField& right) {
    auto left_pad = "  "s;
    auto delimeter = "    "s;
    std::cout << left_pad;
    SeabattleField::PrintDigitLine(std::cout);
    std::cout << delimeter;
    SeabattleField::PrintDigitLine(std::cout);
    std::cout << std::endl;
    for (size_t i = 0; i < SeabattleField::field_size; ++i) {
        std::cout << left_pad;
        left.PrintLine(std::cout, i);
        std::cout << delimeter;
        right.PrintLine(std::cout, i);
        std::cout << std::endl;
    }
    std::cout << left_pad;
    SeabattleField::PrintDigitLine(std::cout);
    std::cout << delimeter;
    SeabattleField::PrintDigitLine(std::cout);
    std::cout << std::endl;
}

template <size_t sz>
static std::optional<std::string> ReadExact(tcp::socket& socket) {
    boost::array<char, sz> buf;
    boost::system::error_code ec;

    net::read(socket, net::buffer(buf), net::transfer_exactly(sz), ec);

    if (ec) {
        return std::nullopt;
    }

    return {{buf.data(), sz}};
}

static bool WriteExact(tcp::socket& socket, std::string_view data) {
    boost::system::error_code ec;

    net::write(socket, net::buffer(data), net::transfer_exactly(data.size()), ec);

    return !ec;
}

class SeabattleAgent {
public:
    SeabattleAgent(const SeabattleField& field)
        : my_field_(field) {
    }

    void StartGame(tcp::socket& socket, bool my_initiative) {
        while (!IsGameEnded()) {
            PrintFields();

            if (my_initiative) {
                MakeTurn(socket);
            } else {
                WaitForTurn(socket);
            }

            my_initiative = !my_initiative;
        }
    }

private:
    static std::optional<std::pair<int, int>> ParseMove(const std::string_view& sv) {
        if (sv.size() != 2) return std::nullopt;

        int p1 = sv[0] - 'A', p2 = sv[1] - '1';

        if (p1 < 0 || p1 > 8) return std::nullopt;
        if (p2 < 0 || p2 > 8) return std::nullopt;

        return {{p1, p2}};
    }

    static std::string MoveToString(std::pair<int, int> move) {
        char buff[] = {static_cast<char>(static_cast<char>(move.first) + 'A'), static_cast<char>(static_cast<char>(move.second) + '1')};
        return {buff, 2};
    }

    void PrintFields() const {
        PrintFieldPair(my_field_, other_field_);
    }

    bool IsGameEnded() const {
        return my_field_.IsLoser() || other_field_.IsLoser();
    }

    std::pair<int, int> ReadMove(tcp::socket& socket) const {
        std::array<char, 2 * sizeof(char)> recv_buf;
        net::read(socket, net::buffer(recv_buf));
        return *ParseMove({recv_buf.data(), recv_buf.size()});
    }

    SeabattleField::ShotResult ReadResult(tcp::socket& socket) const {
        std::array<char, sizeof(char)> recv_buf;
        net::read(socket, net::buffer(recv_buf));
        return static_cast<SeabattleField::ShotResult>(recv_buf[0]);
    }

    void WriteMove(tcp::socket& socket, std::pair<int, int> move) const {
        std::string send_buf = MoveToString(move);
        net::write(socket, net::buffer(send_buf));
    }

    void WriteResult(tcp::socket& socket, SeabattleField::ShotResult result) const {
        std::array<char, sizeof(char)> send_buf = { static_cast<char>(result) };
        net::write(socket, net::buffer(send_buf));
    }

    void ApplyMove(SeabattleField& field, std::pair<int, int> move, SeabattleField::ShotResult result) {
        switch (result) {
        case SeabattleField::ShotResult::MISS:
            field.MarkMiss(move.second, move.first);
            break;
        case SeabattleField::ShotResult::HIT:
            field.MarkHit(move.second, move.first);
            break;
        case SeabattleField::ShotResult::KILL:
            field.MarkKill(move.second, move.first);
            break;
        }
    }

    void MakeTurn(tcp::socket& socket) {
        std::string raw_move;

        std::optional<std::pair<int, int>> move;
        while (!move) {
            std::cout << "Your Turn: ";
            std::cin >> raw_move;
            move = ParseMove(raw_move);
        }

        WriteMove(socket, move.value());
        auto move_result = ReadResult(socket);

        ApplyMove(other_field_, move.value(), move_result);
    }

    void WaitForTurn(tcp::socket& socket) {
        std::cout << "Waiting for turn..." << std::endl;

        auto move = ReadMove(socket);
        auto move_result = my_field_.Shoot(move.second, move.first);

        ApplyMove(my_field_, move, move_result);
        WriteResult(socket, move_result);
    }

private:
    SeabattleField my_field_;
    SeabattleField other_field_;
};

int StartServer(const SeabattleField& field, unsigned short port) {
    SeabattleAgent agent(field);

    net::io_context io_context;
    tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), port));
    std::cout << "Waiting for connection..."sv << std::endl;

    boost::system::error_code ec;
    tcp::socket socket{io_context};
    acceptor.accept(socket, ec);

    if (ec) {
        std::cout << "Can't accept connection"sv << std::endl;
        return 1;
    }

    agent.StartGame(socket, false);
    return 0;
};

int StartClient(const SeabattleField& field, const std::string& ip_str, unsigned short port) {
    SeabattleAgent agent(field);

    boost::system::error_code ec;
    auto endpoint = tcp::endpoint(net::ip::make_address(ip_str, ec), port);

    if (ec) {
        std::cout << "Wrong IP format"sv << std::endl;
        return 1;
    }

    net::io_context io_context;
    tcp::socket socket{io_context};
    socket.connect(endpoint, ec);

    if (ec) {
        std::cout << "Can't connect to server"sv << std::endl;
        return 1;
    }

    agent.StartGame(socket, true);
    return 0;
};

int main(int argc, const char** argv) {
    if (argc != 3 && argc != 4) {
        std::cout << "Usage: program <seed> [<ip>] <port>" << std::endl;
        return 1;
    }

    std::mt19937 engine(std::stoi(argv[1]));
    SeabattleField fieldL = SeabattleField::GetRandomField(engine);

    try {
        if (argc == 3) {
            return StartServer(fieldL, std::stoi(argv[2]));
        } else if (argc == 4) {
            return StartClient(fieldL, argv[2], std::stoi(argv[3]));
        } 
    } catch (const std::exception& exc) {
        std::cout << exc.what() << std::endl;
    }
}
