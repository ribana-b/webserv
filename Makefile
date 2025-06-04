# @--------------------------------------------------------------------------@ #
# |                              Macro Section                               | #
# @--------------------------------------------------------------------------@ #

NAME := webserv

INCLUDE_DIR := include/
SRC_DIR     := src/
OBJ_DIR     := obj/

INCLUDE_FILES := webserv.hpp\

SRC_FILES     := main.cpp\

SRC := $(addprefix $(SRC_DIR), $(SRC_FILES))

VPATH := $(SRC_DIR)\

OBJ := $(patsubst $(SRC_DIR)%.cpp, $(OBJ_DIR)%.o, $(SRC))\

CXX      := clang++
CXXFLAGS := -Wall -Wextra -Werror -MMD -MP -std=c++98 -pedantic
CPPFLAGS := -I $(INCLUDE_DIR)

RM := rm -rf

# @--------------------------------------------------------------------------@ #
# |                              Target Section                              | #
# @--------------------------------------------------------------------------@ #

all: $(NAME)

clean:
	@$(RM) $(OBJ_DIR)
	$(CLEAN_MSG)

fclean: clean
	@$(RM) $(NAME)
	$(FCLEAN_MSG)

re:
	@make -s fclean
	@make -s all

quiet:
	@$(MAKE) -s QUIET=1

.PHONY: all clean fclean re quiet

$(NAME): $(OBJ)
	$(OBJ_MSG)
	@$(CXX) -o $@ $(OBJ) $(CXXFLAGS)
	$(OUTPUT_MSG)

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

$(OBJ_DIR)%.o: %.cpp | $(OBJ_DIR)
	@$(COMPILE_MSG)
	@$(CXX) -o $@ $(CXXFLAGS) $(CPPFLAGS) -c $<

-include $(OBJ:.o=.d)

# @--------------------------------------------------------------------------@ #
# |                              Colour Section                              | #
# @--------------------------------------------------------------------------@ #

T_RED     := \033[31m
T_GREEN   := \033[32m
T_YELLOW  := \033[33m
T_BLUE    := \033[34m
T_WHITE   := \033[37m
T_PINK    := \033[38;2;255;128;255m

BOLD          := \033[1m
ITALIC        := \033[2m
UNDERLINE     := \033[3m
STRIKETHROUGH := \033[4m

CLEAR_LINE := \033[1F\r\033[2K

RESET := \033[0m

# @--------------------------------------------------------------------------@ #
# |                             Message Section                              | #
# @--------------------------------------------------------------------------@ #

SHELL       := /bin/bash

COMPILE_MSG = @echo -e "🌊 🦔 $(T_BLUE)$(BOLD)Compiling $(T_WHITE)$<...$(RESET)"
OBJ_MSG     = @echo -e "✅ 🦔 $(T_PINK)$(BOLD)$(NAME) $(T_YELLOW)Objects $(RESET)$(T_GREEN)created successfully!$(RESET)"
OUTPUT_MSG  = @echo -e "✅ 🦔 $(T_PINK)$(BOLD)$(NAME) $(RESET)$(T_GREEN)created successfully!$(RESET)"
CLEAN_MSG   = @echo -e "🗑️  🦔 $(T_PINK)$(BOLD)$(NAME) $(T_YELLOW)Objects $(RESET)$(T_RED)destroyed successfully!$(RESET)"
FCLEAN_MSG  = @echo -e "🗑️  🦔 $(T_PINK)$(BOLD)$(NAME) $(RESET)$(T_RED)destroyed successfully!$(RESET)"
