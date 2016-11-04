#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/cdev.h>//cdev device
#include <linux/fs.h> //struct file
#include <linux/slab.h>// read RAM
#include <linux/errno.h>//report error
#include <asm/uaccess.h>
#include <linux/types>
#include <linux/fcntl.h>


//file_operations结构->文件操作
struct file_operations scull_fops = {
  .owner = THIS_MODULE,// 初始化为 THIS_MODULE，指向拥有这个结构的模块的指针
  .llseek = scull_llseek,//修改文件中的当前读、写位置，新位置为正返回值
  .read = scull_read,//从设备中获取数据
  .write = scull_write,//发送数据给设备
  .ioctl = scull_ioctl,//发出设备特定命令的方法
  .open = scull_open,//对设备文件进行的第一个操作
  .release = scull_release,//文件结构被释放时引用这个操作
};

//设备注册
struct scull_dev {
  struct scull_qest *data; //
  int quantum;
  int qset;
  unsigned long size;
  unsigned int access_key;
  struct semaphore sem;
  struct cdev cdev;
};

//
struct scull_qest {
  void **data;
  struct scull_qset *next;
};

//设备与内核接口的struct cdev这个结构必须初始化
static void scull_setup_cdev(struct scull_dev *dev, int index) {
  int err, devno = MKDEV(scull_major, scull_minor + index);

  cdev_init(&dev->cdev, &scull_fops);
  dev->cdev.owner = THIS_MODULE;
  dev->cdev.ops = &scull_fops;
  err = cdev_add(&dev->cdev, devno, 1);
  //fail gracefully if need be
  if (err) printk(KERN_NOTICE "Error %d adding scull%d", err, index);
}

//open method
int scull_open(struct inode *inode, struct file *filp) {
  strcut scull_dev *dev;// device information
  //这个宏用来找到适当的设备结构 cdev, cdev saved in inode struct
  dev = container_of(inode->i_cdev, struct scull_dev, cdev);
  filp->private_data = dev;//for other methods

  /* now trim to 0 the length of the device if open was write-only 
    设备为读而打开, 这个操作什么都不做
*/
  if ( (filp->f_flags & O_ACCMODE) == O_WRONLY) {
    scull_trim(dev);/* ignore errors */
  }
  return 0;/* success */
}

int scull_release(strcut inode *inode, struct file *filp) {
  return 0;
}

/* struct scull_dev 和 struct scull_qset 是如何被用来持
有数据的. sucll_trim 函数负责释放整个数据区, 由 scull_open 在文件为写而打开时调
用. 它简单地遍历列表并且释放它发现的任何量子和量子集.
scull_trim 也用在模块清理函数中, 来归还 scull 使用的内存给系统.
*/
int scull_trim(struct scull_dev *dev) {
  struct scull_qset *next, *dptr;
  int qset = dev->qset;
  int i;
  for (dptr = dev->data; dptr; dptr = next) {
    if (dptr->data) {
      for (i = 0; i < qset; i++) {
        kfree(dptr->data[i]);//kfree realse RAM
      }
      kfree(dptr->data);
      dptr->data = NULL;
    }

    next = dptr->next;
    kfree(dptr);
  }
  drv->size = 0;
  dev->quantum = scull_quantum;
  dev->qset = scull_qset;
  dev->data = NULL;
  return 0;
}

/*
如果这个值等于传递给 read 系统调用的 count 参数, 请求的字节数已经被传送.
这是最好的情况.

如果是正数, 但是小于 count, 只有部分数据被传送. 这可能由于几个原因, 依赖
于设备. 常常, 应用程序重新试着读取. 例如, 如果你使用 fread 函数来读取, 库
函数重新发出系统调用直到请求的数据传送完成.

如果值为 0, 到达了文件末尾(没有读取数据).

一个负值表示有一个错误. 这个值指出了什么错误, 根据 <linux/errno.h>. 出错
的典型返回值包括 -EINTR( 被打断的系统调用) 或者 -EFAULT( 坏地址 ).
*/
ssize_t scull_read(struct file *filp, char _user *buf, 
  size_t count, loff_t *f_pos) {
  struct scull_dev *dev = filp->private_data;
  struct scull_qset *dptr;
  int quantum = dev->quantum, qset = dev->qset;
  int itemsize = quantum * qset;
  int item, s_pos, q_pos, rest;
  ssize_t retval = 0;

  if (down_interruptible(&dev->sem))
    return -ERESTARTSYS;
  if (*f_pos >= dev->size)
    goto out;
  if (*f_pos + count > dev->size)
    count = dev->size - *f_pos;

  item = (long)*f_pos / itemsize;
  rest = (long)*f_pos % itemsize;
  s_pos = rest / quantum;
  q_pos = rest % quantum;

  dptr = scull_follow(dev, item);
  if (dptr == NULL || !dptr->data || ! dptr->data[s_pos])
    goto out;

  if (count > quantum - q_pos)
    count = quantum - q_pos;

  if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
    retval = -EFAULT;
    goto out;
  }
  *f_pos += count;
  retval = count;
out:
  up(&dev->sem);
  return retval;
}

/*
如果值等于 count, 要求的字节数已被传送.

如果正值, 但是小于 count, 只有部分数据被传送. 程序最可能重试写入剩下的数
据.

如果值为 0, 什么没有写. 这个结果不是一个错误, 没有理由返回一个错误码. 再
一次, 标准库重试写调用. 我们将在第 6 章查看这种情况的确切含义, 那里介绍了
阻塞.

一个负值表示发生一个错误; 如同对于读, 有效的错误值是定义于
<linux/errno.h>中.

*/
ssize_t scull_write(struct file *filp, const char _user *buf,
  ize_t count, loff_t *f_pos) {
  struct scull_dev *dev = filp->private_data;
  struct scull_qset *dptr;
  int quantum = dev->quantum, qset = dev->qset;
  int itemsize = quantum * qset;
  int item, s_pos, q_pos, rest;
  ssize_t retval = -ENOMEM;
  if (down_interruptible(&dev->sem))
    return -ERESTARTSYS;

  item = (long)*f_pos / itemsize;
  rest = (long)*f_pos % itemsize;
  s_pos = rest / quantum;
  q_pos = rest % quantum;
  /* follow the list up to the right position */
  dptr = scull_follow(dev, item);
  if (dptr == NULL)
    goto out;
  if (!dptr->data) {
    dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
    if (!dptr->data)
      goto out;
    memset(dptr->data, 0, qset * sizeof(char *));
  }
  if (!dptr->data[s_pos]) {
    dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
    if (!dptr->data[s_pos])
      goto out;
  }
  /* write only up to the end of this quantum */
  if (count > quantum - q_pos)
    count = quantum - q_pos;
  if (copy_from_user(dptr->data[s_pos]+q_pos, buf, count)) {
    retval = -EFAULT;
    goto out;
  }
  *f_pos += count;
  retval = count;
  /* update the size */
  if (dev->size < *f_pos)
    dev->size = *f_pos;
out:
  up(&dev->sem);
  return retval;
}


/*

if (scull_major) {
  dev = MKDEV(scull_major, scull_minor);
  //获取一个或多个设备编号来使用
  result = register_chrdev_region(dev, scull_nr_devs, "scull");
} else {
  //动态分配设备编号
  result = alloc_chrdev_region(&dev, scull_minor, scull_nr_devs, "scull");
  scull_major = MAJOR(dev);
}
if (result < 0) {
  printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
  return result;
}
*/
