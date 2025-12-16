/* vcam.h */
#ifndef _VCAM_H_
#define _VCAM_H_

#include <linux/module.h>
#include <linux/init.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <linux/slab.h>
#include <media/v4l2-ioctl.h> // we need v4l2_ioctl_ops
#include <media/videobuf2-v4l2.h>   // we need vb2
#include <media/videobuf2-vmalloc.h> // we need vb2_vmalloc
#include <linux/mutex.h>             // mutex

/* define our main struct for all driver */
struct vcam_device {
    struct v4l2_device v4l2_dev;    // V4L2 parent device
    struct video_device vdev;       // video device node (/dev/videoX)
    struct v4l2_pix_format fmt;     // current format

    // vb2 related members
    struct vb2_queue queue;         // vb2 queue
    struct list_head active_list;   // save data buffer waiting for filling color
    spinlock_t lock;                // spinlock for active_list
    struct mutex queue_lock;        // protect vb2_queue
};

/* * we need to define our buffer struct
 * this is a linked list node, we need to add list_head
 */
struct vcam_buffer {
    struct vb2_v4l2_buffer vb;
    struct list_head list;
};

/* function define for core.c and dev.c to use */
int vcam_setup_video_device(struct vcam_device *vcam);
void vcam_cleanup_video_device(struct vcam_device *vcam);

#endif