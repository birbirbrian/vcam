/* vcam.h */
#ifndef _VCAM_H_
#define _VCAM_H_

#include <linux/module.h>
#include <linux/init.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <linux/slab.h>

/* define our main struct for all driver */
struct vcam_device {
    struct v4l2_device v4l2_dev; // V4L2 parent device
    struct video_device vdev;    // video device node (/dev/videoX)
    
    // we will need to implement more here in the future
    // ex. queue, mutex, format, kthread
};

/* function define for core.c and dev.c to use */
int vcam_setup_video_device(struct vcam_device *vcam);
void vcam_cleanup_video_device(struct vcam_device *vcam);

#endif