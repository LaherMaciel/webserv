# All source files
SRCS_CPP = main.cpp Connection.cpp
SRCS_DIRECTORY = src/
SRCS_LIST = $(addprefix $(SRCS_DIRECTORY), $(SRCS_CPP))

# Header files
HEADER_LIST = webserv.hpp Connection.hpp
HEADER_DIRECTORY = includes/
HEADERS = $(addprefix $(HEADER_DIRECTORY), $(HEADER_LIST))

# Object files
OBJECT_LIST = $(notdir $(SRCS_LIST:.cpp=.o))
OBJECTS_DIRECTORY = objects/
OBJECTS = $(addprefix $(OBJECTS_DIRECTORY), $(OBJECT_LIST))

# name
NAME = webserv

# Compiler settings
CXX = c++
CXXFLAGS = -Wall -Werror -Wextra -std=c++98
INCLUDES = -I$(HEADER_DIRECTORY)

# Colors
RED     = \033[0;31m
GREEN   = \033[0;32m
YELLOW  = \033[0;33m
BLUE    = \033[0;34m
RESET   = \033[0m

# Main target
all: $(NAME)

$(NAME): $(OBJECTS_DIRECTORY) $(OBJECTS)
	@echo "[" "$(YELLOW)..$(RESET)" "] | Compiling files..."
	@if $(CXX) $(CXXFLAGS) $(OBJECTS) -o $(NAME); then \
		echo "[" "$(GREEN)OK$(RESET)" "] | Compilation successful!"; \
		echo "[" "$(GREEN)OK$(RESET)" "] | $(NAME) created successfully!"; \
	else \
		echo "[" "$(RED)Error$(RESET)" "] | An error occurred while creating $(NAME)."; \
		$(MAKE) clean > /dev/null 2>&1; \
		echo "[" "$(RED)Error$(RESET)" "] | All objects cleaned."; \
	fi

# Create objects directory structure
$(OBJECTS_DIRECTORY):
	@echo "[" "$(YELLOW)..$(RESET)" "] | Creating objects directory structure..."
	@mkdir -p $(OBJECTS_DIRECTORY)
	@echo "[" "$(GREEN)OK$(RESET)" "] | Objects directory structure ready!"

# Compile object files
$(OBJECTS_DIRECTORY)%.o : $(SRCS_DIRECTORY)%.cpp $(HEADERS)
	@mkdir -p $(@D)
	@$(CXX) $(CXXFLAGS) -c $(INCLUDES) $< -o $@

# Clean targets
clean:
	@echo "[" "$(YELLOW)..$(RESET)" "] | Removing object files..."
	@rm -rf $(OBJECTS_DIRECTORY)
	@echo "[" "$(GREEN)OK$(RESET)" "] | Object files removed."

fclean: clean
	@echo "[" "$(YELLOW)..$(RESET)" "] | Removing $(NAME)..."
	@rm -rf $(NAME)
	@echo "[" "$(GREEN)OK$(RESET)" "] | $(NAME) removed."
	@echo "[" "$(YELLOW)..$(RESET)" "] | Removing generated shrubbery files..."
	@rm -f *_shrubbery
	@echo "[" "$(GREEN)OK$(RESET)" "] | Shrubbery files removed."

# Rebuild target
re: fclean
	@echo "[" "$(YELLOW)..$(RESET)" "] | Rebuilding $(NAME)..."
	@$(MAKE)

run: $(NAME)
	./$(NAME)

val: $(NAME)
	valgrind ./$(NAME)

macleaks: $(NAME)
	leaks --atExit -- ./$(NAME)

# Help target
help:
	@echo "Available targets:"
	@echo "  $(NAME)     - Build the $(NAME) executable"
	@echo "  clean       - Remove object files"
	@echo "  fclean      - Remove object files and executable"
	@echo "  re          - Rebuild everything"
	@echo "  help        - Show this help message"

.PHONY: all clean fclean re norm help