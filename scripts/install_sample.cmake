if(NOT EXISTS "${DEST}/scene.json")
    file(MAKE_DIRECTORY "${DEST}")
    file(COPY "${SRC}/" DESTINATION "${DEST}")
endif()
