# exo_palm — Seeed XIAO ESP32S3, esp32 core 3.3.1
# Board/port defaults live in sketch.yaml. Override port: make PORT=/dev/cu.usbmodemXXXX
#
# Plain `make` (or `make flash`) builds and uploads.

PORT ?=
PORT_FLAG = $(if $(PORT),-p $(PORT),)

.PHONY: flash compile upload monitor clean intellisense

flash: compile upload

compile:
	arduino-cli compile .

upload:
	arduino-cli upload $(PORT_FLAG) .

monitor:
	arduino-cli monitor $(PORT_FLAG) -c baudrate=115200 .

clean:
	arduino-cli cache clean

# Regenerates compile_commands.json for clangd/IntelliSense
intellisense:
	arduino-cli compile --only-compilation-database --build-path build .
