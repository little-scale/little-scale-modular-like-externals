cmake_minimum_required(VERSION 3.19)

string(REGEX REPLACE "(.*/)(.*)" "\\2" PROJECT_NAME "${CMAKE_CURRENT_SOURCE_DIR}")
project(${PROJECT_NAME})

include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-pretarget.cmake)

include_directories( 
	"${MAX_SDK_INCLUDES}"
	"${MAX_SDK_MSP_INCLUDES}"
)

file(GLOB PROJECT_SRC
     "*.h"
     "*.c"
     "*.cpp"
)

# Exclude backup files from build
list(FILTER PROJECT_SRC EXCLUDE REGEX ".*backup.*")

add_library( 
	${PROJECT_NAME} 
	MODULE
	${PROJECT_SRC}
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-posttarget.cmake)