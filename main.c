#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h> // unix posix api close()

#include <pthread.h>

#define PORT 8000
#define BUFFER_SIZE 1024

typedef struct App App; // Forward declaration

typedef struct {
  char method[10];           // HTTP method
  char path[100];            // Request path
  char headers[BUFFER_SIZE]; // Request headers
  const char *body;          // Request body
  size_t body_length;        // Length of the body
} Request;

typedef struct {
  int status_code;    // HTTP status code
  char *body;         // Response body
  size_t body_length; // Length of the body
} Response;

typedef struct {
  int server_socket;
  struct sockaddr_in server_address;
} NetworkConfig;

typedef struct {
  char *method;
  char *path;
  void (*handler)(const char *request, int client_socket);
} Route;

typedef struct {
  int client_socket;          // The client socket
  pthread_t thread_id;        // The thread handling this client
  struct sockaddr_in address; // Client address
  App *app;
} ClientConnectionThread;

struct App {
  Route *routes;
  ClientConnectionThread **client_connection_thread;
  NetworkConfig *network_config;
  size_t route_count;
  size_t client_count; // Keep track of active client connections
};

// ===============================================================================
// ===============================================================================
// ===============================================================================

int create_socket() {

  // ipv4, tcp connection like , tcp
  int sock = socket(AF_INET, SOCK_STREAM, 0);

  if (sock < 0) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }

  return sock;
}

void bind_socket(App *app) {
  app->network_config->server_address.sin_family = AF_INET;
  app->network_config->server_address.sin_addr.s_addr = INADDR_ANY;
  app->network_config->server_address.sin_port = htons(PORT);

  int bind_result =
      bind(app->network_config->server_socket,
           (struct sockaddr *)&app->network_config->server_address,
           sizeof(app->network_config->server_address));

  if (bind_result < 0) {

    perror("bind failed");
    close(app->network_config->server_socket);
    exit(EXIT_FAILURE);
  }
}

void listen_socket(App *app) {

  if (listen(app->network_config->server_socket, 3) < 0) {

    perror("bind failed");
    close(app->network_config->server_socket);
    exit(EXIT_FAILURE);
  }
  printf("Server is listening on port: %d \n", PORT);
}

// ===============================================================================
// ===============================================================================
// ===============================================================================
// ===============================================================================
// ===============================================================================
// =============================== depreacted ================================
void handle_get_request(const char *request, int client_socket) {
  const char *http_response = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/plain\r\n"
                              "\r\n"
                              "GET request received!\n";
  int sent_bytes = send(client_socket, http_response, strlen(http_response), 0);

  if (sent_bytes < 0) {
    perror("send failed");
  } else {
    printf("Sent response to client socket: %d\n", client_socket);
  }
}

void handle_post_request(const char *request, int client_socket) {
  const char *http_response = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/plain\r\n"
                              "\r\n"
                              "POST request received!\n";
  send(client_socket, http_response, strlen(http_response), 0);
}

void handle_not_found(int client_socket) {
  const char *http_response = "HTTP/1.1 404 Not Found\r\n\r\n";
  send(client_socket, http_response, strlen(http_response), 0);
}
// ===============================================================================
// ===============================================================================
// ===============================================================================
// ===============================================================================
// ===============================================================================
void app_route(App *app, const char *method, const char *path,
               void (*handler)(const char *, int)) {
  app->routes = realloc(app->routes, sizeof(Route) * (app->route_count + 1));
  app->routes[app->route_count].method = strdup(method);
  app->routes[app->route_count].path = strdup(path);
  app->routes[app->route_count].handler = handler;
  app->route_count++;
}
// ===============================================================================
// ===============================================================================
// ===============================================================================
// ===============================================================================
// ===============================================================================

void handle_request(App *app, const char *request, int client_socket) {
  char method[10], path[100];
  sscanf(request, "%s %s", method, path);

  printf("Received request: %s %s\n", method, path); // Log the received request

  printf("------------------------------1 ----------------------\n");

  // request object and response object

  for (size_t i = 0; i < app->route_count; i++) {
    printf("Checking route %zu: Method: %s, Path: %s\n", i,
           app->routes[i].method, app->routes[i].path);
    if (strcmp(app->routes[i].method, method) == 0 &&
        strcmp(app->routes[i].path, path) == 0) {

      printf("they match\n");
      app->routes[i].handler(request, client_socket);
      return;
    }
  }
  // Handle 404 Not Found
  handle_not_found(client_socket);
}
// ===============================================================================
// ===============================================================================
// ===============================================================================
// ===============================================================================
// ===============================================================================

void *thread_execution(void *arg) {

  ClientConnectionThread *connection = (ClientConnectionThread *)arg;

  printf("thread id %d :\n", connection->client_socket);

  char buffer[BUFFER_SIZE];
  int read_size;

  printf("Thread %lu started for client socket: %d\n", pthread_self(),
         connection->client_socket);

  // Read data from the client
  read_size = recv(connection->client_socket, buffer, sizeof(buffer) - 1, 0);
  if (read_size > 0) {
    buffer[read_size] = '\0'; // Null-terminate the string
    handle_request(connection->app, buffer, connection->client_socket);
  }

  // Close the client socket and free the allocated memory
  close(connection->client_socket);
  free(connection);

  printf("Thread %lu finished for client socket: %d\n", pthread_self(),
         connection->client_socket);
  return NULL;
}

// ===============================================================================
// ===============================================================================
// ===============================================================================
// ===============================================================================
// ===============================================================================

void accept_parallel_connections(App *app) {

  app->client_connection_thread = NULL; // Initialize to NULL
  app->client_count = 0;                // Initialize count
                                        //
  while (1) {

    ClientConnectionThread *connection_thread =
        malloc(sizeof(ClientConnectionThread));

    if (connection_thread == NULL) {
      perror("failed to allocate memory for client sockets");
      continue;
    }

    connection_thread->app = app; // Ensure app is set for the thread

    socklen_t client_address_len = sizeof(connection_thread->address);

    connection_thread->client_socket = accept(
        app->network_config->server_socket,
        (struct sockaddr *)&connection_thread->address, &client_address_len);

    if (connection_thread->client_socket >= 0) {

      // Reallocate memory to hold one more client connection
      app->client_connection_thread =
          realloc(app->client_connection_thread,
                  sizeof(ClientConnectionThread *) * (app->client_count + 1));

      if (app->client_connection_thread == NULL) {
        perror("failed to reallocate memory for client connections");
        close(connection_thread->client_socket);
        free(connection_thread);
        continue; // Skip to the next iteration if reallocation fails
      }

      // Store the new connection thread in the array
      app->client_connection_thread[app->client_count] = connection_thread;
      app->client_count++;

      // Log the number of active connections
      printf("Active connections count: %zu\n", app->client_count);

      // maybe bug here for not init app
      int thread_create_result =
          pthread_create(&connection_thread->thread_id, NULL, &thread_execution,
                         (void *)connection_thread);

      if (thread_create_result != 0) {
        perror("failed to create thread");
        close(connection_thread->client_socket);
        free(connection_thread);

      } else {

        pthread_detach(connection_thread->thread_id);
      }

    } else {
      printf("Created thread %lu for client socket: %d\n",
             connection_thread->thread_id, connection_thread->client_socket);
      perror("Accpet connection failed");
      free(connection_thread);
    }
  }
}

// ===============================================================================
// ===============================================================================
// ===============================================================================
// ===============================================================================
// ===============================================================================

void cleanup_app(App *app) {
  // Free routes
  for (size_t i = 0; i < app->route_count; i++) {
    free(app->routes[i].method);
    free(app->routes[i].path);
  }
  free(app->routes);

  // Free client connections
  for (size_t i = 0; i < app->client_count; i++) {
    free(app->client_connection_thread[i]);
  }
  free(app->client_connection_thread);

  // Free network config
  free(app->network_config);

  // Free app itself
  free(app);
}

// ===============================================================================
// ===============================================================================
// ===============================================================================
// ===============================================================================
// ===============================================================================

int main() {

  App *app = malloc(sizeof(App));
  if (app == NULL) {
    perror("failed to allocate memory");
    exit(EXIT_FAILURE);
  }

  // Allocate memory for NetworkConfig struct
  app->network_config = malloc(sizeof(NetworkConfig));
  if (app->network_config == NULL) {
    perror("failed to allocate memory for NetworkConfig");
    free(app); // Free previously allocated memory
    exit(EXIT_FAILURE);
  }

  app->network_config->server_socket = create_socket();
  app->routes = NULL;
  app->route_count = 0;

  // Define routes
  app_route(app, "GET", "/get", handle_get_request);
  app_route(app, "POST", "/post", handle_post_request);

  printf("Number of routes defined: %zu\n", app->route_count);

  bind_socket(app);

  listen_socket(app);

  accept_parallel_connections(app);

  close(app->network_config->server_socket);

  free(app);

  return 0;
}
