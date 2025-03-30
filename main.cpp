#include <iostream>
#include <cstring>      // For memset, strlen
#include <unistd.h>     // For close()
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h> // For sockaddr_in
#include <arpa/inet.h>  // For htons(), inet_ntoa()
#include <thread>       // For std::thread
#include <cstdlib>      // For exit()
#include <fstream>
#include <sys/stat.h>     // For stat()
#include <string>

#define BUFFER_SIZE 1024

// Helper function to determine the content type based on file extension.
std::string get_content_type(const std::string& path) {
    std::size_t dot_pos = path.rfind('.');
    if (dot_pos != std::string::npos) {
        std::string ext = path.substr(dot_pos);
        // Convert extension to lowercase
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (ext == ".html" || ext == ".htm")
            return "text/html";
        else if (ext == ".json")
            return "application/json";
        else if (ext == ".js")
            return "application/javascript";
        else if (ext == ".css")
            return "text/css";
        else if (ext == ".gif")
            return "image/gif";
        else if (ext == ".png")
            return "image/png";
        else if (ext == ".jpg" || ext == ".jpeg")
            return "image/jpeg";
        else if (ext == ".txt")
            return "text/plain";
        else if (ext == ".svg")
            return "image/svg+xml";
        else if (ext == ".ico")
            return "image/x-icon";
        else if (ext == ".pdf")
            return "application/pdf";
        else if (ext == ".zip")
            return "application/zip";
        else if (ext == ".tar")
            return "application/x-tar";
        else if (ext == ".gz")
            return "application/gzip";
        else if (ext == ".mp4")
            return "video/mp4";
        else if (ext == ".mp3")
            return "audio/mpeg";
        else if (ext == ".wav")
            return "audio/wav";
    }
    return "application/octet-stream";
}

void handle_client(int client_socket, sockaddr_in client_address) {
    char buffer[BUFFER_SIZE] = {0};
    int bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);
    if (bytes_read <= 0) {
        close(client_socket);
        return;
    }

    // Parse the request line (e.g., "GET /index.html HTTP/1.1")
    std::istringstream request_stream(buffer);
    std::string method, path, http_version;
    request_stream >> method >> path >> http_version;

    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* timeinfo = std::localtime(&now_time);

    std::string log_message = 
        std::string(inet_ntoa(client_address.sin_addr)) +
         " - - []" +
        [&]() { std::ostringstream oss; oss << std::put_time(timeinfo, "%d/%b/%Y:%H:%M:%S %z"); return oss.str(); }() +
         "] \"" + method + " " + path + " " + http_version + "\" ";

    // Only support GET and OPTIONS requests.
    if (method != "GET" && method != "OPTIONS") {
        std::cout << log_message << "405 0" << std::endl;
        std::string response = "HTTP/1.1 405 Method Not Allowed\r\n"
                               "Content-Length: 0\r\n\r\n";
        send(client_socket, response.c_str(), response.size(), 0);
        close(client_socket);
        return;
    }

    // if path contains "?" or "#", remove them
    std::size_t query_pos = path.find('?');
    if (query_pos != std::string::npos) {
        path = path.substr(0, query_pos);
    }
    std::size_t fragment_pos = path.find('#');
    if (fragment_pos != std::string::npos) {
        path = path.substr(0, fragment_pos);
    }

    // Remove the leading '/' from the path if present.
    while (!path.empty() && path[0] == '/') {
        path.erase(0, 1);
    }

    // If the request is for a directory without a trailing slash,
    // check if it really is a directory and send a redirect.
    if (!path.empty() && path.back() != '/') {
        struct stat path_stat;
        if (stat(path.c_str(), &path_stat) == 0 && S_ISDIR(path_stat.st_mode)) {
            // Build a redirect response to append the trailing slash.
            std::string new_location = "/" + path + "/";
            std::ostringstream redirect;
            redirect << "HTTP/1.1 301 Moved Permanently\r\n"
                     << "Location: " << new_location << "\r\n"
                     << "Content-Length: 0\r\n\r\n";
            std::string redirect_response = redirect.str();
            send(client_socket, redirect_response.c_str(), redirect_response.size(), 0);
            std::cout << log_message << "301 0" << std::endl;
            close(client_socket);
            return;
        }
    }


    // If the path is empty or ends with '/', use index.html.
    if (path.empty() || path.back() == '/') {
        path += "index.html";
    }

    // Prevent directory traversal: reject any path containing ".."
    if (path.find("..") != std::string::npos) {
        std::cout << log_message << "403 0" << std::endl;
        std::string forbidden_response = "HTTP/1.1 403 Forbidden\r\n"
                                         "Content-Length: 0\r\n\r\n";
        send(client_socket, forbidden_response.c_str(), forbidden_response.size(), 0);
        close(client_socket);
        return;
    }

    // Open the requested file in binary mode.
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cout << log_message << "404 0" << std::endl;
        std::string not_found_response = "HTTP/1.1 404 Not Found\r\n"
                                         "Content-Length: 0\r\n\r\n";
        send(client_socket, not_found_response.c_str(), not_found_response.size(), 0);
        close(client_socket);
        return;
    }

    if (method == "OPTIONS") {
        std::cout << log_message << "204 0" << std::endl;
        std::string options_response = "HTTP/1.1 204 No Content\r\n"
                                       "Allow: GET, OPTIONS\r\n"
                                       "Access-Control-Allow-Origin: *\r\n"
                                       "Access-Control-Allow-Methods: GET, OPTIONS\r\n\r\n";
        send(client_socket, options_response.c_str(), options_response.size(), 0);
        close(client_socket);
        return;
    }
    
    // Determine the file size.
    file.seekg(0, std::ios::end);
    std::streampos file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::cout << log_message << "200 " << file_size << std::endl;

    // Determine the content type based on file extension.
    std::string content_type = get_content_type(path);

    // Build and send the HTTP response header.
    std::ostringstream header;
    header << "HTTP/1.1 200 OK\r\n"
           << "Content-Length: " << file_size << "\r\n"
           << "Content-Type: " << content_type << "\r\n"
           << "\r\n";
    std::string header_str = header.str();
    send(client_socket, header_str.c_str(), header_str.size(), 0);

    // Stream the file content in fixed-size chunks.
    const size_t CHUNK_SIZE = 4096;
    char file_buffer[CHUNK_SIZE];
    while (file) {
        file.read(file_buffer, CHUNK_SIZE);
        std::streamsize bytes = file.gcount();
        if (bytes > 0) {
            size_t sent_total = 0;
            while (sent_total < static_cast<size_t>(bytes)) {
                ssize_t sent = send(client_socket, file_buffer + sent_total, bytes - sent_total, 0);
                if (sent <= 0) {
                    // Break out if send fails.
                    break;
                }
                sent_total += sent;
            }
        }
    }

    // Close the file and the client socket.
    file.close();
    close(client_socket);
}

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) {
        try {
            port = std::stoi(argv[1]);
        } catch (const std::exception& e) {
            std::cerr << "Invalid port number: " << argv[1] << "\n";
            return EXIT_FAILURE;
        }
    }

    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    // Create a socket (IPv4, TCP)
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set socket option SO_REUSEADDR
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        exit(EXIT_FAILURE);
    }

    // Define the server address
    address.sin_family = AF_INET;         // IPv4
    address.sin_addr.s_addr = INADDR_ANY;   // Listen on all interfaces
    address.sin_port = htons(port);         // Convert port to network byte order

    // Bind the socket to the specified IP and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "Cannot listen on port " << port << std::endl;
        exit(EXIT_FAILURE);
    }

    // Start listening for incoming connections
    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }
    std::cout << "Multi-threaded TCP server is listening on port " << port << std::endl;

    // Accept new connections in a loop
    while (true) {
        struct sockaddr_in client_address;
        socklen_t client_addrlen = sizeof(client_address);
        int client_socket = accept(server_fd, (struct sockaddr*)&client_address, &client_addrlen);
        if (client_socket < 0) {
            perror("accept failed");
            continue;
        }

        // Create a new thread to handle the client connection
        std::thread t(handle_client, client_socket, client_address);
        t.detach(); // Detach the thread so it cleans up after finishing
    }

    // Close the server socket (this code is actually never reached in this infinite loop)
    close(server_fd);
    return 0;
}