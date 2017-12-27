/*
    Hamitcan MALKOÇ 150140710
    Muhammet UÇAN   150140707

    BLG 413E Project-2

    test.c file included
    in order to compile setup.sh file can be used but module number should be changed
*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/seq_file.h>
#include <linux/cdev.h>
#include <linux/string.h>
#include <asm/segment.h>
#include <linux/buffer_head.h>

#include <asm/switch_to.h>		/* cli(), *_flags */
#include <asm/uaccess.h>	/* copy_*_user */

#include "messagebox_ioctl.h"

enum READ_MODE {
    EXCLUDE_READ = 0,
    INCLUDE_READ = 1
};

#define MESSAGEBOX_MAJOR 0
#define MESSAGEBOX_NR_DEVS 4
#define MESSAGEBOX_GLB_LIM 1000
#define MESSAGEBOX_USR_LIM 100
#define MESSAGEBOX_MODE EXCLUDE_READ

int messagebox_major = MESSAGEBOX_MAJOR;
int messagebox_minor = 0;
int messagebox_nr_devs = MESSAGEBOX_NR_DEVS;
int messagebox_glb_lim = MESSAGEBOX_GLB_LIM;
int messagebox_usr_lim = MESSAGEBOX_USR_LIM;
int messagebox_mode = MESSAGEBOX_MODE;

module_param(messagebox_major, int, S_IRUGO);
module_param(messagebox_minor, int, S_IRUGO);
module_param(messagebox_nr_devs, int, S_IRUGO);
module_param(messagebox_glb_lim, int, S_IRUGO);
module_param(messagebox_usr_lim, int, S_IRUGO);
module_param(messagebox_mode, int, S_IRUGO);

MODULE_AUTHOR("Alessandro Rubini, Jonathan Corbet");
MODULE_LICENSE("Dual BSD/GPL");

struct message {
    char sender[20];
    char receiver[20];
    char msg[100];
    int is_read;
};

struct messagebox_dev {
    struct message *data;
    int global_limit;
    int user_limit;
    int mode;
    unsigned long size;
    struct semaphore sem;
    struct cdev cdev;
};

struct messagebox_dev *messagebox_devices;

int messagebox_trim(struct messagebox_dev *dev) {
    if (dev->data) {
        kfree(dev->data);
    }

    dev->data = NULL;
    dev->size = 0;

    return 0;
}

int messagebox_open(struct inode *inode, struct file *filp) {
    struct messagebox_dev *dev;

    dev = container_of(inode->i_cdev, struct messagebox_dev, cdev);
    filp->private_data = dev;

    /* trim the device if open was write-only */
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
        if (down_interruptible(&dev->sem)) return -ERESTARTSYS;
        messagebox_trim(dev);
        up(&dev->sem);
    }

    return 0;
}

int messagebox_release(struct inode *inode, struct file *filp) {
    return 0;
}

ssize_t messagebox_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    struct messagebox_dev *dev = filp->private_data;
    ssize_t retval = 0;

    int msg_index = 0;
    char *kernel_buffer;

    char receiver[25];

    // gettin uid for username
    kuid_t credd_uid ;
    credd_uid = current->cred->uid; 

    // reading etc passwd
    char *fileBuf;
    struct file *f = NULL;

    fileBuf = kmalloc(4096, GFP_KERNEL);
    if (!fileBuf) {
        kfree(fileBuf);
        goto out;
    }

    f = filp_open("/etc/passwd", O_RDONLY, 0);

    if (f) {
        mm_segment_t old_fs = get_fs();
        set_fs(KERNEL_DS);
        vfs_read(f, fileBuf, 4096, &f->f_pos);
        set_fs(old_fs);

        int file_buffer_index = 0;
        int line_buffer_index = 0;
        char *line_buffer = kmalloc(250, GFP_KERNEL);
        if (!line_buffer) {
            kfree(line_buffer);
            goto out;
        }
    
        for(line_buffer_index = 0; line_buffer_index < 250; line_buffer_index++) {
            line_buffer[line_buffer_index] = 0;
        }
        line_buffer_index = 0;

        for(file_buffer_index = 0; file_buffer_index < strlen(fileBuf); file_buffer_index++) {
            if(fileBuf[file_buffer_index] == '\n' || fileBuf[file_buffer_index] == '\0') {
                int num = 0;
                int uid = 0;
                char name[50] = { 0 };
                int name_index = 0;

                for(line_buffer_index = 0; line_buffer_index < strlen(line_buffer); line_buffer_index++) {
                    if(num == 0) {
                        if(line_buffer[line_buffer_index] == ':') {
                            //we have found the name
                        }
                        else {
                            name[name_index] = line_buffer[line_buffer_index];
                            name_index++;
                        }
                    }

                    if(num == 3) {
                        if(line_buffer[line_buffer_index] == ':') {
                            //we have found the uid
                        }
                        else {
                            uid *= 10;
                            uid += line_buffer[line_buffer_index] - '0';
                        }
                    }

                    if(line_buffer[line_buffer_index] == ':') num++;
                }

                if(uid == credd_uid.val) {
                    strcpy(receiver, name);
                    break;
                }
                line_buffer_index = 0;
            }
            else {
                line_buffer[line_buffer_index] = fileBuf[file_buffer_index];
                line_buffer_index++;
            }
        }

        kfree(line_buffer);
    } else {
        //printk(KERN_ALERT "file open error.\n");
    }

    kfree(fileBuf);

    if (down_interruptible(&dev->sem)) return -ERESTARTSYS;
    if (*f_pos >= dev->size) goto out;

    if (count > 200) count = 200;
    
    kernel_buffer = kmalloc((count + 1) * sizeof(char), GFP_KERNEL);
    if (!kernel_buffer) goto out;
    memset(kernel_buffer, 0, (count + 1) * sizeof(char));
    
    //if (*f_pos + count > dev->size) count = dev->size - *f_pos;

    //f_pos is the message position that we are going to read next

    if (dev->data == NULL) goto out;
    //count is the number of characters we are going to read and send to the user space
    for(msg_index = 0; msg_index < dev->size - *f_pos; msg_index++) {
        struct message msg_info = dev->data[*f_pos + msg_index];

        //we need to check if the receiver name is equal to our name
        if(!strcmp(msg_info.receiver, receiver)) {
            //checking if we already read the message
            //if we have not red it or the ALL variable is 1 we are going to push it to kernel buffer and user space afterwards
            if(!msg_info.is_read || dev->mode) {
                //current length of the string inside kernel buffer + sender info + message info + 4 characters (' ', ':', ' ', '\n')
                //if the addition above is greater than the count we are going to exceed the character limit that kernel buffer can hold
                //loop has to stop, not the user but from user space something will make another read call

                //let say our count was 5000 at first, we made it 4000 because it exceeded our read limit
                //and at the end we read 3980 characters, we could not make it to 4000 because it would excedd the kernel buffer limit
                //we are going to return 3980 as retval, and OS will make another read call for rest of the 1120 characters
                //and it will continue untill we reach the end of file which is retval = 0 or if we reach the number of characters OS wants
                if(strlen(kernel_buffer) + strlen(msg_info.sender) + strlen(msg_info.msg) + 4 > count) {
                    break;
                }

                strcat(kernel_buffer, msg_info.sender);
                strcat(kernel_buffer, " : ");
                strcat(kernel_buffer, msg_info.msg);
                strcat(kernel_buffer, "\n");
                dev->data[*f_pos + msg_index].is_read = 1;
            }
        }
    }

    if (copy_to_user(buf, kernel_buffer, strlen(kernel_buffer))) {        
        retval = -EFAULT;
        kfree(kernel_buffer);
        goto out;
    }

    *f_pos += msg_index;
    retval = strlen(kernel_buffer);
    kfree(kernel_buffer);

  out:
    up(&dev->sem);
    return retval;
}

ssize_t messagebox_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    struct messagebox_dev *dev = filp->private_data;
    ssize_t retval = -ENOMEM;

    char sender[25];

    int msg_index = 0;
    char *kernel_buffer;
    
    struct message *message_info;

    char *msg_inf, *end;
    char *receiver, *msg;

    char char_num = 0;

    // gettin uid for username
    kuid_t credd_uid ;
    credd_uid = current->cred->uid; 

    // reading etc passwd
    char *fileBuf;
    struct file *f = NULL;

    fileBuf = kmalloc(4096, GFP_KERNEL);
    if (!fileBuf) {
        kfree(fileBuf);
        goto out;
    }

    f = filp_open("/etc/passwd", O_RDONLY, 0);

    if (f) {
        mm_segment_t old_fs = get_fs();
        set_fs(KERNEL_DS);
        vfs_read(f, fileBuf, 4096, &f->f_pos);
        set_fs(old_fs);

        int file_buffer_index = 0;
        int line_buffer_index = 0;
        char *line_buffer = kmalloc(250, GFP_KERNEL);
        if (!line_buffer) {
            kfree(line_buffer);
            goto out;
        }
    
        for(line_buffer_index = 0; line_buffer_index < 250; line_buffer_index++) {
            line_buffer[line_buffer_index] = 0;
        }
        line_buffer_index = 0;

        for(file_buffer_index = 0; file_buffer_index < strlen(fileBuf); file_buffer_index++) {
            if(fileBuf[file_buffer_index] == '\n' || fileBuf[file_buffer_index] == '\0') {
                int num = 0;
                int uid = 0;
                char name[50] = { 0 };
                int name_index = 0;

                for(line_buffer_index = 0; line_buffer_index < strlen(line_buffer); line_buffer_index++) {
                    if(num == 0) {
                        if(line_buffer[line_buffer_index] == ':') {
                            //we have found the name
                        }
                        else {
                            name[name_index] = line_buffer[line_buffer_index];
                            name_index++;
                        }
                    }

                    if(num == 3) {
                        if(line_buffer[line_buffer_index] == ':') {
                            //we have found the uid
                        }
                        else {
                            uid *= 10;
                            uid += line_buffer[line_buffer_index] - '0';
                        }
                    }

                    if(line_buffer[line_buffer_index] == ':') num++;
                }

                if(uid == credd_uid.val) {
                    strcpy(sender, name);
                    break;
                }
                line_buffer_index = 0;
            }
            else {
                line_buffer[line_buffer_index] = fileBuf[file_buffer_index];
                line_buffer_index++;
            }
        }

        kfree(line_buffer);
    } else {
        //printk(KERN_ALERT "file open error.\n");
    }

    kfree(fileBuf);

    if (down_interruptible(&dev->sem)) return -ERESTARTSYS;

    if (*f_pos >= dev->global_limit) {
        retval = 0;
        goto out;
    }

    if (!dev->data) {
        dev->data = kmalloc(dev->global_limit * sizeof(struct message), GFP_KERNEL);
        if (!dev->data) goto out;
        memset(dev->data, 0, dev->global_limit * sizeof(struct message));
    }

    if (count > 200) count = 200;

    kernel_buffer = kmalloc((count + 1) * sizeof(char), GFP_KERNEL);
    if (!kernel_buffer) goto out;
    memset(kernel_buffer, 0, (count + 1) * sizeof(char));
    
    msg_inf = kernel_buffer;
    end = kernel_buffer;
    
    receiver = msg_inf;
    msg = msg_inf;

    if (copy_from_user(kernel_buffer, buf, count)) {
        retval = -EFAULT;
        kfree(kernel_buffer);
        goto out;
    }
    
    //parsing the input. it can contain more than one message
    //input starts with @. at the beginning of the loop when we use strsep it will split the string into two strings
    //one of them will be empty string other one will be the rest of the message + receiver name without @ character
    //first loop wont do anything since we add a message if the msg_inf is not empyt string
    //that is why we started the i from -1
    for(msg_index = -1; msg_index + *f_pos < dev->global_limit && msg_inf != NULL; msg_index++) {
        int number_of_messages = 0;
        int i = 0;
    
        strsep(&end, "@");
        char_num++;
        
        if(strcmp(msg_inf, "")) {//we have rest of the message
            receiver = msg_inf;
            msg = msg_inf;
            
            strsep(&msg, " ");
            char_num++;

            if(receiver && msg) {
                if(strcmp(msg, "") && strcmp(receiver, "")) {
                    for(i = 0; i < *f_pos + msg_index; i++) {
                        if(!strcmp(dev->data[i].receiver, receiver)) {
                            number_of_messages++;
                        }
                    }

                    if(number_of_messages < dev->user_limit) {
                        message_info = kmalloc(sizeof(struct message), GFP_KERNEL);
                        
                        strcpy(message_info->msg, msg);
                        strcpy(message_info->sender, sender);
                        strcpy(message_info->receiver, receiver);
                        message_info->is_read = 0;
            
                        dev->data[*f_pos + msg_index] = *message_info;
        
                        char_num += (strlen(msg) + strlen(receiver));
    
                        kfree(message_info);
                    }
                    else {

                    }
                }
                else {
                    char_num -= 2;
                    break;
                }
            }
            else {
                char_num -= 2;
                break;
            }
        }
    
        msg_inf = end;
    }

    *f_pos += msg_index;
    retval = (!char_num) ? count : --char_num;

    kfree(kernel_buffer);

    /* update the size */
    if (dev->size < *f_pos) dev->size = *f_pos;

  out:
    up(&dev->sem);
    return retval;
}

long messagebox_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
	int err = 0, tmp;
	int retval = 0;

	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != MESSAGEBOX_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > MESSAGEBOX_IOC_MAXNR) return -ENOTTY;

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ) err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE) err =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	if (err) return -EFAULT;
    //it changes the global variables not the driver's variable
	switch(cmd) {
        case MESSAGEBOX_IOCRESET:
            messagebox_glb_lim = MESSAGEBOX_GLB_LIM;
            messagebox_usr_lim = MESSAGEBOX_USR_LIM;
            messagebox_mode = MESSAGEBOX_MODE;
            break;

        case MESSAGEBOX_IOCSMODE: /* Set: arg points to the value */
            if (! capable (CAP_SYS_ADMIN)) return -EPERM;
            retval = __get_user(messagebox_mode, (int __user *)arg);
            break;

        case MESSAGEBOX_IOCTMODE: /* Tell: arg is the value */
            if (! capable (CAP_SYS_ADMIN)) return -EPERM;
            messagebox_mode = arg;
            break;

        case MESSAGEBOX_IOCGMODE: /* Get: arg is pointer to result */
            retval = __put_user(messagebox_mode, (int __user *)arg);
            break;

        case MESSAGEBOX_IOCQMODE: /* Query: return it (it's positive) */
            return messagebox_mode;

        case MESSAGEBOX_IOCXMODE: /* eXchange: use arg as pointer */
            if (! capable (CAP_SYS_ADMIN)) return -EPERM;
            tmp = messagebox_mode;
            retval = __get_user(messagebox_mode, (int __user *)arg);
            if (retval == 0) retval = __put_user(tmp, (int __user *)arg);
            break;

        case MESSAGEBOX_IOCHMODE: /* sHift: like Tell + Query */
            if (! capable (CAP_SYS_ADMIN)) return -EPERM;
            tmp = messagebox_mode;
            messagebox_mode = arg;
            return tmp;

        case MESSAGEBOX_IOCSGLIM:
            if (! capable (CAP_SYS_ADMIN)) return -EPERM;
            retval = __get_user(messagebox_glb_lim, (int __user *)arg);
            break;

        case MESSAGEBOX_IOCTGLIM:
            if (! capable (CAP_SYS_ADMIN)) return -EPERM;
            messagebox_glb_lim = arg;
            break;

        case MESSAGEBOX_IOCGGLIM:
            retval = __put_user(messagebox_glb_lim, (int __user *)arg);
            break;

        case MESSAGEBOX_IOCQGLIM:
            return messagebox_glb_lim;

        case MESSAGEBOX_IOCXGLIM:
            if (! capable (CAP_SYS_ADMIN)) return -EPERM;
            tmp = messagebox_glb_lim;
            retval = __get_user(messagebox_glb_lim, (int __user *)arg);
            if (retval == 0) retval = put_user(tmp, (int __user *)arg);
            break;

        case MESSAGEBOX_IOCHGLIM:
            if (! capable (CAP_SYS_ADMIN)) return -EPERM;
            tmp = messagebox_glb_lim;
            messagebox_glb_lim = arg;
            return tmp;

        case MESSAGEBOX_IOCSULIM:
            if (! capable (CAP_SYS_ADMIN)) return -EPERM;
            retval = __get_user(messagebox_usr_lim, (int __user *)arg);
            break;

        case MESSAGEBOX_IOCTULIM:
            if (! capable (CAP_SYS_ADMIN)) return -EPERM;
            messagebox_usr_lim = arg;
            break;

        case MESSAGEBOX_IOCGULIM:
            retval = __put_user(messagebox_usr_lim, (int __user *)arg);
            break;

        case MESSAGEBOX_IOCQULIM:
            return messagebox_usr_lim;

        case MESSAGEBOX_IOCXULIM:
            if (! capable (CAP_SYS_ADMIN)) return -EPERM;
            tmp = messagebox_usr_lim;
            retval = __get_user(messagebox_usr_lim, (int __user *)arg);
            if (retval == 0) retval = put_user(tmp, (int __user *)arg);
            break;

        case MESSAGEBOX_IOCHULIM:
            if (! capable (CAP_SYS_ADMIN)) return -EPERM;
            tmp = messagebox_usr_lim;
            messagebox_usr_lim = arg;
            return tmp;
            
        default:  /* redundant, as cmd was checked against MAXNR */
		    return -ENOTTY;
    }
    return retval;
}

loff_t messagebox_llseek(struct file *filp, loff_t off, int whence) {
    struct messagebox_dev *dev = filp->private_data;
    loff_t newpos;

    switch(whence) {
        case 0: /* SEEK_SET */
            newpos = off;
            break;

        case 1: /* SEEK_CUR */
            newpos = filp->f_pos + off;
            break;

        case 2: /* SEEK_END */
            newpos = dev->size + off;
            break;

        default: /* can't happen */
            return -EINVAL;
    }
    if (newpos < 0) return -EINVAL;
    
    filp->f_pos = newpos;
    
    return newpos;
}

struct file_operations messagebox_fops = {
    .owner =    THIS_MODULE,
    .llseek =   messagebox_llseek,
    .read =     messagebox_read,
    .write =    messagebox_write,
    .unlocked_ioctl =  messagebox_ioctl,
    .open =     messagebox_open,
    .release =  messagebox_release,
};


void messagebox_cleanup_module(void) {
    int i;
    dev_t devno = MKDEV(messagebox_major, messagebox_minor);

    if (messagebox_devices) {
        for (i = 0; i < messagebox_nr_devs; i++) {
            messagebox_trim(messagebox_devices + i);
            cdev_del(&messagebox_devices[i].cdev);
        }
    kfree(messagebox_devices);
    }

    unregister_chrdev_region(devno, messagebox_nr_devs);
}

int messagebox_init_module(void) {
    int result, i;
    int err;
    dev_t devno = 0;
    struct messagebox_dev *dev;
    if (messagebox_major) {
        devno = MKDEV(messagebox_major, messagebox_minor);
        result = register_chrdev_region(devno, messagebox_nr_devs, "messagebox");
    }
    else {
        result = alloc_chrdev_region(&devno, messagebox_minor, messagebox_nr_devs, "messagebox");
        messagebox_major = MAJOR(devno);
    }
    if (result < 0) {
        printk(KERN_WARNING "messagebox: can't get major %d\n", messagebox_major);
        return result;
    }

    messagebox_devices = kmalloc(messagebox_nr_devs * sizeof(struct messagebox_dev), GFP_KERNEL);
    if (!messagebox_devices) {
        result = -ENOMEM;
        goto fail;
    }
    memset(messagebox_devices, 0, messagebox_nr_devs * sizeof(struct messagebox_dev));

    /* Initialize each device. */
    for (i = 0; i < messagebox_nr_devs; i++) {
        dev = &messagebox_devices[i];
        dev->global_limit = messagebox_glb_lim;
        dev->user_limit = messagebox_usr_lim;
        dev->mode = messagebox_mode;
        sema_init(&dev->sem,1);
        devno = MKDEV(messagebox_major, messagebox_minor + i);
        cdev_init(&dev->cdev, &messagebox_fops);
        dev->cdev.owner = THIS_MODULE;
        dev->cdev.ops = &messagebox_fops;
        err = cdev_add(&dev->cdev, devno, 1);
        if (err) printk(KERN_NOTICE "Error %d adding messagebox%d", err, i);
    }

    return 0; /* succeed */

  fail:
    messagebox_cleanup_module();
    return result;
}

module_init(messagebox_init_module);
module_exit(messagebox_cleanup_module);
