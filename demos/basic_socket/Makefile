NAME := webserv_v0
CXX := c++
CXXFLAGS := -Wall -Wextra -Werror -std=c++98

SRC := webserv_test2.cpp
OBJ := $(SRC:.cpp=.o)

all: $(NAME)

$(NAME): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $(NAME)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ)

fclean: clean
	rm -f $(NAME)

run: $(NAME)
	./$(NAME)

re: fclean all

.PHONY: all clean fclean re
