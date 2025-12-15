/* vcam_dev.c */
#include "vcam.h"

#define VCAM_DEV_NAME "vcam"
/* support format */
#define VCAM_WIDTH  640
#define VCAM_HEIGHT 480
#define VCAM_PIX_FMT V4L2_PIX_FMT_RGB24 // 24-bit RGB

const char *vcam_dev_name = VCAM_DEV_NAME;

/* * V4L2 ioctl operations
 * We need to implement these functions: vcam_querycap, vcam_enum_fmt, vcam_g_fmt, vcam_s_fmt, vcam_try_fmt
 * These function is used to handle ioctl calls from user space applications.
 */

static int vcam_querycap(struct file *file,
                         void *priv,
                         struct v4l2_capability *cap)
{
    strcpy(cap->driver, vcam_dev_name);
    strcpy(cap->card, vcam_dev_name);
    strcpy(cap->bus_info, "platform: virtual");
    cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
                        V4L2_CAP_READWRITE | V4L2_CAP_DEVICE_CAPS;

    return 0;
}

static int vcam_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{

	if (f->index != 0)
		return -EINVAL;

	f->pixelformat = V4L2_PIX_FMT_UYVY;
	return 0;
}

static int vcam_g_fmt_vid_cap(struct file *file, void *priv,
                              struct v4l2_format *f)
{
    struct vcam_device *vcam = (struct vcam_device *) video_drvdata(file);

    /* return current format */
    f->fmt.pix = vcam->fmt;
    return 0;
}

/* * Try format
Here we just accept the format and store it in our device struct
 */
static int vcam_try_fmt_vid_cap(struct file *file, void *priv,
                                struct v4l2_format *f)
{
    f->fmt.pix.pixelformat = VCAM_PIX_FMT;
    f->fmt.pix.width = VCAM_WIDTH;
    f->fmt.pix.height = VCAM_HEIGHT;
    f->fmt.pix.field = V4L2_FIELD_NONE;
    f->fmt.pix.bytesperline = VCAM_WIDTH * 3; // RGB24 = 3 bytes per pixel
    f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * VCAM_HEIGHT;
    return 0;
}

/* * Set format
 */
static int vcam_s_fmt_vid_cap(struct file *file, void *priv,
                              struct v4l2_format *f)
{
    int ret;

    struct vcam_device *vcam = (struct vcam_device *) video_drvdata(file);

    // run try first to validate
    ret = vcam_try_fmt_vid_cap(file, priv, f);
    if (ret < 0)
        return ret;

    // set format
    vcam->fmt = f->fmt.pix;
    pr_debug("Resolution set to %dx%d\n", vcam->fmt.width, vcam->fmt.height);
    return 0;
}

/* ioctl table */
static const struct v4l2_ioctl_ops vcam_ioctl_ops = {
    .vidioc_querycap      = vcam_querycap,
    .vidioc_enum_fmt_vid_cap = vcam_enum_fmt_vid_cap,
    .vidioc_g_fmt_vid_cap = vcam_g_fmt_vid_cap,
    .vidioc_try_fmt_vid_cap = vcam_try_fmt_vid_cap,
    .vidioc_s_fmt_vid_cap = vcam_s_fmt_vid_cap,
};

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
    .unlocked_ioctl = video_ioctl2, // set to video_ioctl2 to handle ioctl calls
    // we will add more option here in the future
    // ex. .unlocked_ioctl = video_ioctl2,
};

static void vcam_video_release(struct video_device *vdev) {
    // release will be done in module_exit
    pr_info("VCAM: Video device released (callback)\n");
}

/*
 * This function sets up the video device and registers it with the V4L2 framework.
 */
int vcam_setup_video_device(struct vcam_device *vcam)
{
    int ret;

    /* init default format */
    vcam->fmt.width = VCAM_WIDTH;
    vcam->fmt.height = VCAM_HEIGHT;
    vcam->fmt.pixelformat = VCAM_PIX_FMT;
    vcam->fmt.field = V4L2_FIELD_NONE;
    vcam->fmt.bytesperline = VCAM_WIDTH * 3;
    vcam->fmt.sizeimage = vcam->fmt.bytesperline * VCAM_HEIGHT;

    /* init video_device */
    vcam->vdev.v4l2_dev = &vcam->v4l2_dev;    // set Parent
    vcam->vdev.fops = &vcam_fops;             // set file operations
    vcam->vdev.release = vcam_video_release;
    vcam->vdev.ioctl_ops = &vcam_ioctl_ops; // set ioctl operations
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