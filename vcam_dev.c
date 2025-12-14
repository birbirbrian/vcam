/* vcam_dev.c */
#include "vcam.h"

/* * File Operations 
 */
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
    // we will add more option here in the future
    // ex. .unlocked_ioctl = video_ioctl2,
};

static void vcam_video_release(struct video_device *vdev) {
    pr_info("VCAM: Video device released (callback)\n");
}

/*
 * This function sets up the video device and registers it with the V4L2 framework.
 */
int vcam_setup_video_device(struct vcam_device *vcam)
{
    int ret;

    /* init video_device */
    vcam->vdev.v4l2_dev = &vcam->v4l2_dev;    // set Parent
    vcam->vdev.fops = &vcam_fops;             // set file operations
    vcam->vdev.release = vcam_video_release;  
    vcam->vdev.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
    
    // use this function to save driver data pointer
    // so we can get our vcam_device struct from video_device struct
    video_set_drvdata(&vcam->vdev, vcam); 
    strscpy(vcam->vdev.name, "my_vcam", sizeof(vcam->vdev.name));

    /* register to /dev/videoX */
    ret = video_register_device(&vcam->vdev, VFL_TYPE_VIDEO, -1);
    if (ret < 0) {
        pr_err("VCAM: Failed to register video device\n");
        return ret;
    }

    return 0;
}

void vcam_cleanup_video_device(struct vcam_device *vcam)
{
    video_unregister_device(&vcam->vdev);
}