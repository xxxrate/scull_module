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
  struct scull_qest *data;
  int quantum;
  int qset;
  unsigned long size;
  unsigned int access_key;
  struct semaphore sem;
  struct cdev cdev;
}

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
