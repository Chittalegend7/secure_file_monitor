#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace std;

constexpr int PORT = 8080;
constexpr int BUFFER_SIZE = 4096;
constexpr long long MAX_FILE_SIZE = 100LL * 1024 * 1024;

const string STORAGE_DIR = "storage";
const string USERNAME = "admin";
const string PASSWORD = "admin123";

// ------------------------------------------------------------
// Send exactly all requested bytes
// ------------------------------------------------------------
bool send_all(int socket_fd, const char* data, size_t length) {
    size_t total_sent = 0;

    while (total_sent < length) {
        ssize_t sent = send(
            socket_fd,
            data + total_sent,
            length - total_sent,
            0
        );

        if (sent <= 0) {
            return false;
        }

        total_sent += static_cast<size_t>(sent);
    }

    return true;
}

// ------------------------------------------------------------
// Receive exactly the requested number of bytes
// ------------------------------------------------------------
bool recv_all(int socket_fd, char* data, size_t length) {
    size_t total_received = 0;

    while (total_received < length) {
        ssize_t received = recv(
            socket_fd,
            data + total_received,
            length - total_received,
            0
        );

        if (received <= 0) {
            return false;
        }

        total_received += static_cast<size_t>(received);
    }

    return true;
}

// ------------------------------------------------------------
// Receive a line ending with '\n'
// ------------------------------------------------------------
bool recv_line(int socket_fd, string& line) {
    line.clear();

    char ch;

    while (true) {
        ssize_t received = recv(socket_fd, &ch, 1, 0);

        if (received <= 0) {
            return false;
        }

        if (ch == '\n') {
            return true;
        }

        if (ch != '\r') {
            line += ch;
        }

        if (line.size() > 4096) {
            return false;
        }
    }
}

// ------------------------------------------------------------
// SHA-256 using OpenSSL EVP API
// ------------------------------------------------------------
string sha256_file(const string& filename) {
    ifstream file(filename, ios::binary);

    if (!file) {
        return "";
    }

    EVP_MD_CTX* context = EVP_MD_CTX_new();

    if (context == nullptr) {
        return "";
    }

    if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(context);
        return "";
    }

    char buffer[BUFFER_SIZE];

    while (file.good()) {
        file.read(buffer, sizeof(buffer));
        streamsize bytes_read = file.gcount();

        if (bytes_read > 0) {
            if (EVP_DigestUpdate(
                    context,
                    buffer,
                    static_cast<size_t>(bytes_read)) != 1) {

                EVP_MD_CTX_free(context);
                return "";
            }
        }
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_length = 0;

    if (EVP_DigestFinal_ex(
            context,
            hash,
            &hash_length) != 1) {

        EVP_MD_CTX_free(context);
        return "";
    }

    EVP_MD_CTX_free(context);

    stringstream result;

    for (unsigned int i = 0; i < hash_length; ++i) {
        result << hex
               << setw(2)
               << setfill('0')
               << static_cast<int>(hash[i]);
    }

    return result.str();
}

// ------------------------------------------------------------
// Validate filename against path traversal
// ------------------------------------------------------------
bool is_safe_filename(const string& filename) {
    if (filename.empty() || filename.size() > 255) {
        return false;
    }

    if (filename == "." || filename == "..") {
        return false;
    }

    if (filename.find("..") != string::npos) {
        return false;
    }

    if (filename.find('/') != string::npos) {
        return false;
    }

    if (filename.find('\\') != string::npos) {
        return false;
    }

    return true;
}

// ------------------------------------------------------------
// Authentication
// ------------------------------------------------------------
bool authenticate(const string& username, const string& password) {
    return username == USERNAME && password == PASSWORD;
}

// ------------------------------------------------------------
// LIST command
// ------------------------------------------------------------
void handle_list(int client_socket) {
    DIR* directory = opendir(STORAGE_DIR.c_str());

    if (directory == nullptr) {
        send_all(client_socket, "ERR STORAGE\n",
                 strlen("ERR STORAGE\n"));
        return;
    }

    struct dirent* entry;

    while ((entry = readdir(directory)) != nullptr) {

        string filename = entry->d_name;

        if (filename == "." || filename == "..") {
            continue;
        }

        string full_path = STORAGE_DIR + "/" + filename;

        struct stat file_info{};

        if (stat(full_path.c_str(), &file_info) == 0) {

            if (S_ISREG(file_info.st_mode)) {

                string response = filename + "\n";

                if (!send_all(
                        client_socket,
                        response.c_str(),
                        response.size())) {

                    closedir(directory);
                    return;
                }
            }
        }
    }

    closedir(directory);

    const string end_message = "END\n";

    send_all(
        client_socket,
        end_message.c_str(),
        end_message.size()
    );
}

// ------------------------------------------------------------
// PUT command
// ------------------------------------------------------------
void handle_put(
    int client_socket,
    const string& filename,
    long long file_size,
    const string& expected_hash) {

    if (!is_safe_filename(filename)) {
        const string response = "ERR INVALID_FILENAME\n";

        send_all(
            client_socket,
            response.c_str(),
            response.size()
        );

        return;
    }

    if (file_size < 0 || file_size > MAX_FILE_SIZE) {
        const string response = "ERR FILE_TOO_LARGE\n";

        send_all(
            client_socket,
            response.c_str(),
            response.size()
        );

        return;
    }

    string final_path = STORAGE_DIR + "/" + filename;
    string temp_path = final_path + ".tmp";

    ofstream output(temp_path, ios::binary);

    if (!output) {
        const string response = "ERR CREATE_FILE\n";

        send_all(
            client_socket,
            response.c_str(),
            response.size()
        );

        return;
    }

    const string ready = "READY\n";

    if (!send_all(
            client_socket,
            ready.c_str(),
            ready.size())) {

        output.close();
        remove(temp_path.c_str());
        return;
    }

    char buffer[BUFFER_SIZE];
    long long remaining = file_size;

    while (remaining > 0) {

        size_t chunk_size =
            static_cast<size_t>(
                min<long long>(remaining, BUFFER_SIZE)
            );

        if (!recv_all(
                client_socket,
                buffer,
                chunk_size)) {

            output.close();
            remove(temp_path.c_str());
            return;
        }

        output.write(buffer, chunk_size);

        if (!output) {
            output.close();
            remove(temp_path.c_str());

            const string response = "ERR WRITE_FILE\n";

            send_all(
                client_socket,
                response.c_str(),
                response.size()
            );

            return;
        }

        remaining -= static_cast<long long>(chunk_size);
    }

    output.close();

    string actual_hash = sha256_file(temp_path);

    if (actual_hash.empty()) {
        remove(temp_path.c_str());

        const string response = "ERR HASH\n";

        send_all(
            client_socket,
            response.c_str(),
            response.size()
        );

        return;
    }

    if (actual_hash != expected_hash) {
        remove(temp_path.c_str());

        const string response = "ERR INTEGRITY\n";

        send_all(
            client_socket,
            response.c_str(),
            response.size()
        );

        return;
    }

    if (rename(
            temp_path.c_str(),
            final_path.c_str()) != 0) {

        remove(temp_path.c_str());

        const string response = "ERR SAVE\n";

        send_all(
            client_socket,
            response.c_str(),
            response.size()
        );

        return;
    }

    string response = "OK PUT " + actual_hash + "\n";

    send_all(
        client_socket,
        response.c_str(),
        response.size()
    );
}

// ------------------------------------------------------------
// GET command
// ------------------------------------------------------------
void handle_get(
    int client_socket,
    const string& filename) {

    if (!is_safe_filename(filename)) {
        const string response = "ERR INVALID_FILENAME\n";

        send_all(
            client_socket,
            response.c_str(),
            response.size()
        );

        return;
    }

    string file_path = STORAGE_DIR + "/" + filename;

    struct stat file_info{};

    if (stat(file_path.c_str(), &file_info) != 0 ||
        !S_ISREG(file_info.st_mode)) {

        const string response = "ERR NOT_FOUND\n";

        send_all(
            client_socket,
            response.c_str(),
            response.size()
        );

        return;
    }

    long long file_size =
        static_cast<long long>(file_info.st_size);

    if (file_size > MAX_FILE_SIZE) {
        const string response = "ERR FILE_TOO_LARGE\n";

        send_all(
            client_socket,
            response.c_str(),
            response.size()
        );

        return;
    }

    string file_hash = sha256_file(file_path);

    if (file_hash.empty()) {
        const string response = "ERR HASH\n";

        send_all(
            client_socket,
            response.c_str(),
            response.size()
        );

        return;
    }

    string header =
        "SIZE " +
        to_string(file_size) +
        "\nSHA256 " +
        file_hash +
        "\n";

    if (!send_all(
            client_socket,
            header.c_str(),
            header.size())) {

        return;
    }

    string ready;

    if (!recv_line(client_socket, ready)) {
        return;
    }

    if (ready != "READY") {
        return;
    }

    ifstream input(file_path, ios::binary);

    if (!input) {
        return;
    }

    char buffer[BUFFER_SIZE];

    while (input.good()) {

        input.read(buffer, sizeof(buffer));

        streamsize bytes_read = input.gcount();

        if (bytes_read > 0) {

            if (!send_all(
                    client_socket,
                    buffer,
                    static_cast<size_t>(bytes_read))) {

                return;
            }
        }
    }
}

// ------------------------------------------------------------
// Handle one authenticated client
// ------------------------------------------------------------
void handle_client(int client_socket, string client_address) {

    cout << "[+] Client connected: "
         << client_address
         << endl;

    // Authentication
    string auth_line;

    if (!recv_line(client_socket, auth_line)) {
        close(client_socket);
        return;
    }

    string command;
    string username;
    string password;

    stringstream auth_stream(auth_line);

    auth_stream >> command >> username >> password;

    if (command != "AUTH" ||
        !authenticate(username, password)) {

        const string response = "ERR AUTH\n";

        send_all(
            client_socket,
            response.c_str(),
            response.size()
        );

        close(client_socket);

        cout << "[-] Authentication failed: "
             << client_address
             << endl;

        return;
    }

    const string auth_success = "OK AUTH\n";

    if (!send_all(
            client_socket,
            auth_success.c_str(),
            auth_success.size())) {

        close(client_socket);
        return;
    }

    cout << "[+] Client authenticated: "
         << client_address
         << endl;

    // Command loop
    while (true) {

        string line;

        if (!recv_line(client_socket, line)) {
            break;
        }

        stringstream stream(line);

        string operation;
        stream >> operation;

        // ---------------- LIST ----------------
        if (operation == "LIST") {

            handle_list(client_socket);
        }

        // ---------------- PUT ----------------
        else if (operation == "PUT") {

            string filename;
            long long file_size;
            string file_hash;

            stream >> filename >> file_size >> file_hash;

            if (filename.empty() ||
                file_size < 0 ||
                file_hash.empty()) {

                const string response = "ERR BAD_PUT\n";

                send_all(
                    client_socket,
                    response.c_str(),
                    response.size()
                );

                continue;
            }

            handle_put(
                client_socket,
                filename,
                file_size,
                file_hash
            );
        }

        // ---------------- GET ----------------
        else if (operation == "GET") {

            string filename;

            stream >> filename;

            if (filename.empty()) {

                const string response = "ERR BAD_GET\n";

                send_all(
                    client_socket,
                    response.c_str(),
                    response.size()
                );

                continue;
            }

            handle_get(
                client_socket,
                filename
            );
        }

        // ---------------- QUIT ----------------
        else if (operation == "QUIT") {

            const string response = "BYE\n";

            send_all(
                client_socket,
                response.c_str(),
                response.size()
            );

            break;
        }

        // ---------------- Unknown command ----------------
        else {

            const string response = "ERR UNKNOWN_COMMAND\n";

            send_all(
                client_socket,
                response.c_str(),
                response.size()
            );
        }
    }

    close(client_socket);

    cout << "[-] Client disconnected: "
         << client_address
         << endl;
}

// ------------------------------------------------------------
// Main server
// ------------------------------------------------------------
int main() {

    // Create storage directory
    if (mkdir(STORAGE_DIR.c_str(), 0755) != 0 &&
        errno != EEXIST) {

        cerr << "Error: unable to create storage directory."
             << endl;

        return 1;
    }

    int server_socket =
        socket(AF_INET, SOCK_STREAM, 0);

    if (server_socket < 0) {

        cerr << "Error creating socket: "
             << strerror(errno)
             << endl;

        return 1;
    }

    int option = 1;

    if (setsockopt(
            server_socket,
            SOL_SOCKET,
            SO_REUSEADDR,
            &option,
            sizeof(option)) < 0) {

        cerr << "Error setting socket options."
             << endl;

        close(server_socket);

        return 1;
    }

    sockaddr_in server_address{};

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);

    if (bind(
            server_socket,
            reinterpret_cast<sockaddr*>(&server_address),
            sizeof(server_address)) < 0) {

        cerr << "Error binding socket: "
             << strerror(errno)
             << endl;

        close(server_socket);

        return 1;
    }

    if (listen(server_socket, 10) < 0) {

        cerr << "Error starting server."
             << endl;

        close(server_socket);

        return 1;
    }

    cout << "========================================"
         << endl;

    cout << " Secure File Monitor Server"
         << endl;

    cout << "========================================"
         << endl;

    cout << "Server listening on port "
         << PORT
         << endl;

    cout << "Storage directory: "
         << STORAGE_DIR
         << endl;

    cout << "Username: "
         << USERNAME
         << endl;

    cout << "Password: "
         << PASSWORD
         << endl;

    cout << "Waiting for clients..."
         << endl;

    while (true) {

        sockaddr_in client_address{};
        socklen_t client_length =
            sizeof(client_address);

        int client_socket =
            accept(
                server_socket,
                reinterpret_cast<sockaddr*>(&client_address),
                &client_length
            );

        if (client_socket < 0) {

            cerr << "Error accepting client: "
                 << strerror(errno)
                 << endl;

            continue;
        }

        char client_ip[INET_ADDRSTRLEN];

        inet_ntop(
            AF_INET,
            &client_address.sin_addr,
            client_ip,
            INET_ADDRSTRLEN
        );

        string client_address_string =
            string(client_ip) +
            ":" +
            to_string(
                ntohs(client_address.sin_port)
            );

        // One thread for each connected client
        thread(
            handle_client,
            client_socket,
            client_address_string
        ).detach();
    }

    close(server_socket);

    return 0;
}
