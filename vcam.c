#include <linux/module.h>
#include <linux/init.h>
#include <media/v4l2-device.h>  // V4L2 kernel structures
#include <media/v4l2-dev.h> // struct video_device
#include <linux/slab.h>        // kzalloc/kfree

/* Custom Driver Structure. This structure holds all context data needed for our virtual camera device */
struct vcam_device {
    struct v4l2_device v4l2_dev;
    struct video_device vdev;
};

/* Global pointer to our device data */
static struct vcam_device *vcam;

/* file operations */
static int vcam_open(struct file *filp) {
    pr_info("VCAM: Device opened!\n");
    return 0;
}

static int vcam_release(struct file *filp) {
    pr_info("VCAM: Device closed!\n");
    return 0;
}

static const struct v4l2_file_operations vcam_fops = {
    .owner = THIS_MODULE,
    .open = vcam_open,
    .release = vcam_release,
};

static void vcam_video_release(struct video_device *vdev) {
    pr_err("VCAM: vcam_video_release function\n");

}

static int __init vcam_init(void)
{
    int ret;

    pr_info("VCAM: Initializing Virtual Camera Module\n");

    /* Locate memory for our custom structure */ 
    vcam = kzalloc(sizeof(struct vcam_device), GFP_KERNEL);
    if (!vcam) {
        pr_err("VCAM: Failed to allocate memory for vcam_device\n");
        return -ENOMEM;
    }

    /* need a name if want to use NULL device */
    strscpy(vcam->v4l2_dev.name, "vcam_core", sizeof(vcam->v4l2_dev.name));

    /* Register V4L2 Device */
    ret = v4l2_device_register(NULL, &vcam->v4l2_dev);

    if (ret) { 
        pr_err("VCAM: Failed to register v4l2 device (ret=%d)\n", ret);
        kfree(vcam);
        return ret;
    }

    /* Initial video device */
    vcam->vdev.v4l2_dev = &vcam->v4l2_dev;    // point to v4l2 device(parent)
    vcam->vdev.fops = &vcam_fops;             // point to file operations
    vcam->vdev.release = vcam_video_release;  // release function
    vcam->vdev.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
    strscpy(vcam->vdev.name, "my_vcam", sizeof(vcam->vdev.name)); // device name

    /* regist vedio device, this will expose /dev/device-node */
    ret = video_register_device(&vcam->vdev, VFL_TYPE_VIDEO, -1);
    if (ret < 0) {
        pr_err("VCAM: Failed to register video device\n");
        goto video_regdev_failure;
    }

    pr_info("VCAM: Virtual Camera Module initialized successfully\n");
    return 0;

video_regdev_failure:
    v4l2_device_unregister(&vcam->v4l2_dev);
    kfree(vcam);
    return ret;
}

static void __exit vcam_exit(void)
{
    pr_info("VCAM: Exiting Virtual Camera Module\n");

    /* Cleanup code for the virtual camera module goes here */
    video_unregister_device(&vcam->vdev);
    v4l2_device_unregister(&vcam->v4l2_dev);
    kfree(vcam);

    pr_info("VCAM: Virtual Camera Module exited successfully\n");
}

module_init(vcam_init);
module_exit(vcam_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Brian");
MODULE_DESCRIPTION("Virtual V4L2 compatible camera device driver");

