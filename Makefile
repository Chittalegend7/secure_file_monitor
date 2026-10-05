CXX = g++

CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread

OPENSSL_LIBS = -lssl -lcrypto

SERVER = server/server
CLIENT = client/client
MONITOR = monitor/monitor

all: $(SERVER) $(CLIENT) $(MONITOR)

$(SERVER): server/server.cpp
	$(CXX) $(CXXFLAGS) server/server.cpp -o $(SERVER) $(OPENSSL_LIBS)

$(CLIENT): client/client.cpp
	$(CXX) $(CXXFLAGS) client/client.cpp -o $(CLIENT) $(OPENSSL_LIBS)

$(MONITOR): monitor/monitor.cpp
	$(CXX) $(CXXFLAGS) monitor/monitor.cpp -o $(MONITOR)

clean:
	rm -f $(SERVER) $(CLIENT) $(MONITOR)

run-server:
	./$(SERVER)

run-client:
	./$(CLIENT)

run-monitor:
	./$(MONITOR)

.PHONY: all clean run-server run-client run-monitor
