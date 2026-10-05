#include <arpa/inet.h>
#include <openssl/evp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using namespace std;

constexpr int SERVER_PORT = 8080;
constexpr int BUFFER_SIZE = 4096;

const string SERVER_IP = "127.0.0.1";

// ------------------------------------------------------------
// Send all bytes
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
// Receive exactly requested bytes
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
// Receive one line
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
// SHA-256 file hash
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

    if (EVP_DigestInit_ex(
            context,
            EVP_sha256(),
            nullptr) != 1) {

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
// Send command
// ------------------------------------------------------------
bool send_command(int socket_fd, const string& command) {
    string message = command + "\n";

    return send_all(
        socket_fd,
        message.c_str(),
        message.size()
    );
}

// ------------------------------------------------------------
// Connect to server
// ------------------------------------------------------------
int connect_to_server() {

    int socket_fd =
        socket(AF_INET, SOCK_STREAM, 0);

    if (socket_fd < 0) {
        cerr << "Error creating socket.\n";
        return -1;
    }

    sockaddr_in server_address{};

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(SERVER_PORT);

    if (inet_pton(
            AF_INET,
            SERVER_IP.c_str(),
            &server_address.sin_addr) <= 0) {

        cerr << "Invalid server address.\n";
        close(socket_fd);
        return -1;
    }

    if (connect(
            socket_fd,
            reinterpret_cast<sockaddr*>(&server_address),
            sizeof(server_address)) < 0) {

        cerr << "Unable to connect to server.\n";
        close(socket_fd);
        return -1;
    }

    return socket_fd;
}

// ------------------------------------------------------------
// LIST files
// ------------------------------------------------------------
void list_files(int socket_fd) {

    if (!send_command(socket_fd, "LIST")) {
        cerr << "Failed to send LIST command.\n";
        return;
    }

    cout << "\nFiles available on server:\n";
    cout << "---------------------------\n";

    while (true) {

        string line;

        if (!recv_line(socket_fd, line)) {
            cerr << "Connection lost.\n";
            return;
        }

        if (line == "END") {
            break;
        }

        cout << line << '\n';
    }

    cout << "---------------------------\n";
}

// ------------------------------------------------------------
// Upload file
// ------------------------------------------------------------
void upload_file(
    int socket_fd,
    const string& filename) {

    ifstream input(filename, ios::binary);

    if (!input) {
        cerr << "Cannot open file: "
             << filename
             << '\n';

        return;
    }

    input.seekg(0, ios::end);

    long long file_size =
        static_cast<long long>(input.tellg());

    input.seekg(0, ios::beg);

    string file_hash = sha256_file(filename);

    if (file_hash.empty()) {
        cerr << "Unable to calculate file hash.\n";
        return;
    }

    cout << "\nUploading: " << filename << '\n';
    cout << "Size: " << file_size << " bytes\n";
    cout << "SHA-256: " << file_hash << '\n';

    string command =
        "PUT " +
        filename +
        " " +
        to_string(file_size) +
        " " +
        file_hash;

    if (!send_command(socket_fd, command)) {
        cerr << "Failed to send PUT command.\n";
        return;
    }

    string response;

    if (!recv_line(socket_fd, response)) {
        cerr << "Connection lost.\n";
        return;
    }

    if (response != "READY") {
        cerr << "Server rejected upload: "
             << response
             << '\n';

        return;
    }

    char buffer[BUFFER_SIZE];

    long long remaining = file_size;

    while (remaining > 0) {

        size_t chunk_size =
            static_cast<size_t>(
                min<long long>(
                    remaining,
                    BUFFER_SIZE
                )
            );

        input.read(
            buffer,
            static_cast<streamsize>(chunk_size)
        );

        streamsize bytes_read =
            input.gcount();

        if (bytes_read <= 0) {
            cerr << "File read error.\n";
            return;
        }

        if (!send_all(
                socket_fd,
                buffer,
                static_cast<size_t>(bytes_read))) {

            cerr << "Upload connection failed.\n";
            return;
        }

        remaining -=
            static_cast<long long>(bytes_read);
    }

    if (!recv_line(socket_fd, response)) {
        cerr << "Connection lost after upload.\n";
        return;
    }

    if (response.rfind("OK PUT ", 0) == 0) {

        string server_hash =
            response.substr(7);

        cout << "\nUpload successful!\n";
        cout << "Server SHA-256: "
             << server_hash
             << '\n';

        if (server_hash == file_hash) {
            cout << "Integrity check: PASSED\n";
        } else {
            cout << "Integrity check: FAILED\n";
        }

    } else {
        cout << "Upload failed: "
             << response
             << '\n';
    }
}

// ------------------------------------------------------------
// Download file
// ------------------------------------------------------------
void download_file(
    int socket_fd,
    const string& filename) {

    if (!send_command(
            socket_fd,
            "GET " + filename)) {

        cerr << "Failed to send GET command.\n";
        return;
    }

    string response;

    if (!recv_line(socket_fd, response)) {
        cerr << "Connection lost.\n";
        return;
    }

    if (response.rfind("SIZE ", 0) != 0) {

        cerr << "Download failed: "
             << response
             << '\n';

        return;
    }

    long long file_size = 0;

    try {
        file_size =
            stoll(response.substr(5));
    }
    catch (...) {
        cerr << "Invalid file size received.\n";
        return;
    }

    if (!recv_line(socket_fd, response)) {
        cerr << "Connection lost.\n";
        return;
    }

    if (response.rfind("SHA256 ", 0) != 0) {
        cerr << "Invalid SHA-256 response.\n";
        return;
    }

    string expected_hash =
        response.substr(7);

    string output_filename =
        "downloaded_" + filename;

    ofstream output(
        output_filename,
        ios::binary
    );

    if (!output) {
        cerr << "Cannot create output file.\n";
        return;
    }

    if (!send_command(socket_fd, "READY")) {
        cerr << "Failed to confirm download.\n";
        return;
    }

    cout << "\nDownloading: "
         << filename
         << '\n';

    cout << "Size: "
         << file_size
         << " bytes\n";

    char buffer[BUFFER_SIZE];

    long long remaining = file_size;

    while (remaining > 0) {

        size_t chunk_size =
            static_cast<size_t>(
                min<long long>(
                    remaining,
                    BUFFER_SIZE
                )
            );

        if (!recv_all(
                socket_fd,
                buffer,
                chunk_size)) {

            cerr << "Download connection failed.\n";
            output.close();
            return;
        }

        output.write(
            buffer,
            static_cast<streamsize>(chunk_size)
        );

        if (!output) {
            cerr << "Error writing downloaded file.\n";
            output.close();
            return;
        }

        remaining -=
            static_cast<long long>(chunk_size);
    }

    output.close();

    string actual_hash =
        sha256_file(output_filename);

    cout << "\nDownload successful!\n";
    cout << "Saved as: "
         << output_filename
         << '\n';

    cout << "Expected SHA-256: "
         << expected_hash
         << '\n';

    cout << "Actual SHA-256:   "
         << actual_hash
         << '\n';

    if (actual_hash == expected_hash) {
        cout << "Integrity check: PASSED\n";
    } else {
        cout << "Integrity check: FAILED\n";
    }
}

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------
int main() {

    cout << "========================================\n";
    cout << " Secure File Monitor Client\n";
    cout << "========================================\n";

    int socket_fd = connect_to_server();

    if (socket_fd < 0) {
        return 1;
    }

    cout << "Connected to server.\n";

    string username;
    string password;

    cout << "\nUsername: ";
    getline(cin, username);

    cout << "Password: ";
    getline(cin, password);

    string auth_command =
        "AUTH " +
        username +
        " " +
        password;

    if (!send_command(
            socket_fd,
            auth_command)) {

        cerr << "Failed to send authentication.\n";
        close(socket_fd);
        return 1;
    }

    string response;

    if (!recv_line(socket_fd, response)) {
        cerr << "Server disconnected.\n";
        close(socket_fd);
        return 1;
    }

    if (response != "OK AUTH") {

        cerr << "Authentication failed.\n";

        close(socket_fd);
        return 1;
    }

    cout << "Authentication successful!\n";

    while (true) {

        cout << "\n========================================\n";
        cout << "1. List server files\n";
        cout << "2. Upload file\n";
        cout << "3. Download file\n";
        cout << "4. Quit\n";
        cout << "========================================\n";

        cout << "Enter choice: ";

        string choice;
        getline(cin, choice);

        // LIST
        if (choice == "1") {

            list_files(socket_fd);
        }

        // UPLOAD
        else if (choice == "2") {

            string filename;

            cout << "Enter local filename: ";
            getline(cin, filename);

            if (filename.empty()) {
                cout << "Filename cannot be empty.\n";
                continue;
            }

            upload_file(
                socket_fd,
                filename
            );
        }

        // DOWNLOAD
        else if (choice == "3") {

            string filename;

            cout << "Enter server filename: ";
            getline(cin, filename);

            if (filename.empty()) {
                cout << "Filename cannot be empty.\n";
                continue;
            }

            download_file(
                socket_fd,
                filename
            );
        }

        // QUIT
        else if (choice == "4") {

            send_command(
                socket_fd,
                "QUIT"
            );

            if (recv_line(
                    socket_fd,
                    response)) {

                cout << response << '\n';
            }

            break;
        }

        else {
            cout << "Invalid choice.\n";
        }
    }

    close(socket_fd);

    cout << "Client closed.\n";

    return 0;
}
