rmmod messagebox
rm /dev/messagebox0

make

insmod messagebox.ko
mknod /dev/messagebox0 c 243 0 # 243 module number
chmod o+w /dev/messagebox0
chmod g+w /dev/messagebox0