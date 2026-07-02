ip=STM8Flasher.local
BIN := $(wildcard /tmp/arduino-build/*.ino.bin)


# Push a target image to the running flasher, e.g.: make image IHX=~/proj/app.ihx
image:
	curl -f -F 'ihx=@$(IHX)' http://$(ip)/upload

# T-Display S3: 16MB flash, OPI PSRAM (per Xinyuan-LilyGO/T-Display-S3 README)
bin:
	@ arduino-cli compile --build-path /tmp/arduino-build -b esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi .


flash:
	make ota

usb:
	make serial

serial:
	- pkill -9 -f microcom
	arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc /tmp/arduino-build --input-dir /tmp/arduino-build

ota:
	python3 "/home/holla/.arduino15/packages/esp32/hardware/esp32/3.3.0/tools/espota.py" -r -i $(ip) -p 3232 --auth=admin -f "$(BIN)"
