cmake_minimum_required(VERSION 2.8)

# Just add all judges and nothing else
add_subdirectory(codex_judge)
add_subdirectory(judge_passthrough)
add_subdirectory(filter)
add_subdirectory(shuffled)
add_subdirectory(recodex_token_judge)
