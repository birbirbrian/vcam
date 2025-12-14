/* vcam_core.c */
#include "vcam.h"

static struct vcam_device *vcam;

static int __init vcam_init(void)
{
    int ret;

    pr_info("VCAM: Initializing module...\n");

    /* locate memory for vcam_device */
    vcam = kzalloc(sizeof(struct vcam_device), GFP_KERNEL);
    if (!vcam) {
        return -ENOMEM;
    }

    /* register parent */
    strscpy(vcam->v4l2_dev.name, "vcam_core", sizeof(vcam->v4l2_dev.name));
    ret = v4l2_device_register(NULL, &vcam->v4l2_dev);
    if (ret) {
        pr_err("VCAM: Failed to register v4l2 device\n");
        kfree(vcam);
        return ret;
    }

    /* call function in vcam_dev.c to register device node*/
    ret = vcam_setup_video_device(vcam);
    if (ret) {
        goto video_reg_failed;
    }

    pr_info("VCAM: Module initialized successfully\n");
    return 0;

video_reg_failed:
    v4l2_device_unregister(&vcam->v4l2_dev);
    kfree(vcam);
    return ret;
}

static void __exit vcam_exit(void)
{
    pr_info("VCAM: Exiting module...\n");

    if (vcam) {
        vcam_cleanup_video_device(vcam);    // unregister /dev/videoX
        v4l2_device_unregister(&vcam->v4l2_dev); // unregister V4L2 parent
        kfree(vcam); // free memory
    }
}

module_init(vcam_init);
module_exit(vcam_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Brian");
MODULE_DESCRIPTION("Virtual V4L2 Camera Driver (Split Version)");