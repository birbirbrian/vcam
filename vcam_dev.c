/* vcam_dev.c */
#include "vcam.h"

#define VCAM_DEV_NAME "vcam"
/* support format */
#define VCAM_WIDTH  640
#define VCAM_HEIGHT 480
#define VCAM_PIX_FMT V4L2_PIX_FMT_RGB24 // 24-bit RGB

static const u8 vcam_colorbar[8][3] = {
	{255, 255, 255}, /* White   */
	{255, 255,   0}, /* Yellow  */
	{  0, 255, 255}, /* Cyan    */
	{  0, 255,   0}, /* Green   */
	{255,   0, 255}, /* Magenta */
	{255,   0,   0}, /* Red     */
	{  0,   0, 255}, /* Blue    */
	{  0,   0,   0}, /* Black   */
};

const char *vcam_dev_name = VCAM_DEV_NAME;

static int vcam_s_ctrl(struct v4l2_ctrl *ctrl)
{
    struct vcam_device *vcam = container_of(ctrl->handler, struct vcam_device, ctrl_handler);

    // handle all control cases
    switch(ctrl->id){
        case V4L2_CID_EXPOSURE:
            vcam->exposure = ctrl->val;
            pr_info("VCAM: Exposure set to %d\n", vcam->exposure);
            break;
        default:
            return -EINVAL;
    }
    return 0;
}

/* * Control operations
 */ 
static const struct v4l2_ctrl_ops vcam_ctrl_ops = {
    .s_ctrl = vcam_s_ctrl,
};

static inline u8 clamp_u8(int val)
{
    if(val < 0)
        return 0;
    if(val > 255)
        return 255;
    return (u8)val;
}

/* * vcam fill color bar
 * fill different color bar into buffer base on algorithm
 */
static void vcam_fill_color_bar(u8 *vaddr, unsigned int width, unsigned int height, unsigned int shift, s32 exposure)
{
    unsigned int x, y;
    unsigned int bar_width = width / 8; // 畫面切成 8 等份
    u8 *pixel = vaddr;
    u8 scale_color[8][3];

    // chage exposure, scale my color structure with exposure weight
    for(int i = 0; i < 8; i++ ) {
        scale_color[i][0] = clamp_u8((vcam_colorbar[i][0] * exposure) / 255); // R
        scale_color[i][1] = clamp_u8((vcam_colorbar[i][1] * exposure) / 255); // G
        scale_color[i][2] = clamp_u8((vcam_colorbar[i][2] * exposure) / 255); // B
    }

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            /* * algorithm：
             * 1. x / bar_width: which bar we are in(we will need this to choose color)
             * 2. + shift: let bar shift, actually we are shifting color index
             * 3. % 8: make sure it is in 0~7 range, we only have 8 colors
             */
            int color_index = ((x / bar_width) + shift) % 8;

            // write pixel (RGB24)
            pixel[0] = scale_color[color_index][0]; // R
            pixel[1] = scale_color[color_index][1]; // G
            pixel[2] = scale_color[color_index][2]; // B
            
            pixel += 3; // move 3 bytes
        }
    }
}

/* * Kthread function
 * This is a thread that will simulate camera and generate image data
 */
static int vcam_kthread(void *data)
{
    struct vcam_device *vcam = data;
    struct vcam_buffer *buf;
    unsigned long flags;

    // define time
    int timeout_jiffies = msecs_to_jiffies(33); // ~30 FPS

    pr_info("VCAM: Thread started running\n");

    // infinite loop, if no one calls kthread_stop, it will keep running
    while(!kthread_should_stop()) {

        // sleep
        // set ourself to be intruptible sleep
        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout(timeout_jiffies);

        if (kthread_should_stop())
            break;

        // get buffer from active_list
        // we need to use spinlock to protect the list
        spin_lock_irqsave(&vcam->lock, flags);

        // if there is no buffer available for us to fill data, we juxt unlock and continue
        if(list_empty(&vcam->active_list)) {
            // no buffer, unlock and we continue to wait for next loop
            spin_unlock_irqrestore(&vcam->lock, flags);
            continue;
        }

        // get the buffer from active_list
        buf = list_first_entry(&vcam->active_list, struct vcam_buffer, list);
        list_del(&buf->list); // remove current buffer from active_list

        spin_unlock_irqrestore(&vcam->lock, flags);

        // fill buffer with data
        void *vaddr = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
        unsigned int width = vcam->fmt.width;
        unsigned int height = vcam->fmt.height;

        if(vaddr) {
            // imeplemnt new color bar fill function
            // cam->sequence / 5 -> I want to change bar every 5 frames
            // so 5 frames will be a set and they will be the same
            vcam_fill_color_bar(vaddr, width, height, vcam->sequence / 5, vcam->exposure);
        }

        // set timestamp
        buf->vb.vb2_buf.timestamp = ktime_get_ns();
        buf->vb.sequence = vcam->sequence++;

        // tell vb2 framework that buffer is done
        vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
    }

    pr_info("VCAM: Thread stopped\n");
    return 0;
}

/* * VB2 buffer operations
 * We need to implement these functions: vcam_queue_setup, vcam_buf_prepare, vcam_buf_queue, vcam_start_streaming,
 * vcam_stop_streaming
 * These function is used to handle quene from user space.
 */

/* * Queue setup
 * Check if the buffer size is enough for our format or we need to give the size we support
 */
static int vcam_queue_setup(struct vb2_queue *q,
			   unsigned int *nbuffers, unsigned int *nplanes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
    // get our device struct
    struct vcam_device *vcam = vb2_get_drv_priv(q);

    // support format size
    unsigned long size = vcam->fmt.sizeimage;

if (*nplanes) {
        // check nplanes numbers(normal case is 1)
        if (*nplanes != 1)
            return -EINVAL;

        // check the request size is enough (can not small than current image size)
        if (sizes[0] < size)
            return -EINVAL;

        return 0;
    }

    *nplanes = 1;         // RGB24 = 1 plane
    sizes[0] = size;      // set buffer size
    return 0;
}

/* * Buffer prepare
 * Prepare/check the buffer before queueing
 */
static int vcam_buf_prepare(struct vb2_buffer *vb)
{
    // we need to use vb2_queue to get our device struct(parent struct)
    struct vcam_device *vcam = vb2_get_drv_priv(vb->vb2_queue);
    unsigned long size = vcam->fmt.sizeimage;

    // check buffer size
    if (vb2_plane_size(vb, 0) < size) {
        pr_err("VCAM: Buffer too small\n");
        return -EINVAL;
    }

    // set payload (real data size)
    vb2_set_plane_payload(vb, 0, size);
    return 0;
}

/* * Buffer queue
 * When call this function, user space application has queued a buffer
 * We need to add it to our active_list for further processing
 */
static void vcam_buf_queue(struct vb2_buffer *vb)
{
    struct vcam_device *vcam = vb2_get_drv_priv(vb->vb2_queue);

    // use container_of to get vcam_buffer
    struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
    struct vcam_buffer *buf = container_of(vbuf, struct vcam_buffer, vb);
    unsigned long flags;

    spin_lock_irqsave(&vcam->lock, flags);
    list_add_tail(&buf->list, &vcam->active_list); // add to active_list
    spin_unlock_irqrestore(&vcam->lock, flags);
}

/* * Start streaming
 */
static int vcam_start_streaming(struct vb2_queue *vq, unsigned int count)
{
    struct vcam_device *vcam = vb2_get_drv_priv(vq);
    pr_info("VCAM: Start streaming (Queue enabled)\n");

    vcam->sequence = 0;

    // start kthread
    vcam->kthread = kthread_run(vcam_kthread, vcam, "vcam-worker");

    // if 
    if(IS_ERR(vcam->kthread)) {
        pr_err("VCAM: Failed to create kernel thread\n");

        struct vcam_buffer *buf;
        unsigned long flags;
        spin_lock_irqsave(&vcam->lock, flags);
        while (!list_empty(&vcam->active_list)) {
            buf = list_first_entry(&vcam->active_list, struct vcam_buffer, list);
            list_del(&buf->list);
            vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_QUEUED);
        }
        spin_unlock_irqrestore(&vcam->lock, flags);
        return PTR_ERR(vcam->kthread);
    }

    pr_info("VCAM: Start streaming (Thread running)\n");
    return 0;
}

/* * Stop streaming
 */
static void vcam_stop_streaming(struct vb2_queue *vq)
{
    struct vcam_device *vcam = vb2_get_drv_priv(vq);
    struct vcam_buffer *buf;
    unsigned long flags;

    pr_info("VCAM: Stop streaming\n");

    // stop kthread
    if (vcam->kthread) {
        kthread_stop(vcam->kthread);
        vcam->kthread = NULL;
    }

    /* * when streaming is off, we need to return all buffers in active_list
     * to VB2 framework, otherwise application will hang.
     */
    spin_lock_irqsave(&vcam->lock, flags);
    while (!list_empty(&vcam->active_list)) {
        buf = list_first_entry(&vcam->active_list, struct vcam_buffer, list);
        list_del(&buf->list);

        // return state: error
        vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
    }
    spin_unlock_irqrestore(&vcam->lock, flags);
    pr_info("VCAM: Stop streaming\n");
}

static const struct vb2_ops vcam_vb2_ops = {
    .queue_setup     = vcam_queue_setup,
    .buf_prepare     = vcam_buf_prepare,
    .buf_queue       = vcam_buf_queue,
    .start_streaming = vcam_start_streaming,
    .stop_streaming  = vcam_stop_streaming,
    .wait_prepare    = vb2_ops_wait_prepare, // standerd helper: release lock
    .wait_finish     = vb2_ops_wait_finish,  // standerd helper: acquire lock
};


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

	f->pixelformat = VCAM_PIX_FMT;
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

    // add vb2 ioctl operations
    .vidioc_reqbufs       = vb2_ioctl_reqbufs,
    .vidioc_create_bufs   = vb2_ioctl_create_bufs,
    .vidioc_prepare_buf   = vb2_ioctl_prepare_buf,
    .vidioc_querybuf      = vb2_ioctl_querybuf,
    .vidioc_qbuf          = vb2_ioctl_qbuf,
    .vidioc_dqbuf         = vb2_ioctl_dqbuf,
    .vidioc_streamon      = vb2_ioctl_streamon,
    .vidioc_streamoff     = vb2_ioctl_streamoff,
};

/* * File Operations 
 */
static int vcam_open(struct file *filp) {
    pr_info("VCAM: Device opened!\n");
    return v4l2_fh_open(filp);
}

static int vcam_release(struct file *filp) {
    pr_info("VCAM: Device closed!\n");
    vb2_fop_release(filp);
    return v4l2_fh_release(filp);
}

static const struct v4l2_file_operations vcam_fops = {
    .owner = THIS_MODULE,
    .open = vcam_open,
    .release = vcam_release,
    .unlocked_ioctl = video_ioctl2, // set to video_ioctl2 to handle ioctl calls
    .mmap = vb2_fop_mmap,
    .poll = vb2_fop_poll,
    // we will add more option here in the future
    // ex. .unlocked_ioctl = video_ioctl2,
};

static void vcam_video_release(struct video_device *vdev) {
    // release will be done in module_exit
    pr_info("VCAM: Video device released (callback)\n");
}

/*
 * This function sets up the video device and registers it with the V4L2 framework.
 * Init list, lock, format, vb2 queue, video device
 */
int vcam_setup_video_device(struct vcam_device *vcam)
{
    int ret;
    struct vb2_queue *q = &vcam->queue;

    /* init list and lock */
    INIT_LIST_HEAD(&vcam->active_list);
    spin_lock_init(&vcam->lock);
    mutex_init(&vcam->queue_lock); // protect vb2_queue

    /* init default format */
    vcam->fmt.width = VCAM_WIDTH;
    vcam->fmt.height = VCAM_HEIGHT;
    vcam->fmt.pixelformat = VCAM_PIX_FMT;
    vcam->fmt.field = V4L2_FIELD_NONE;
    vcam->fmt.bytesperline = VCAM_WIDTH * 3;
    vcam->fmt.sizeimage = vcam->fmt.bytesperline * VCAM_HEIGHT;

    /* init vb2 queue */
    q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ; // support modes
    q->drv_priv = vcam;
    q->buf_struct_size = sizeof(struct vcam_buffer);
    q->ops = &vcam_vb2_ops;          // register callback
    q->mem_ops = &vb2_vmalloc_memops;// use vmalloc (CPU 存取)
    q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
    q->lock = &vcam->queue_lock;     // set mutex lock

    ret = vb2_queue_init(q); // after this, queue is ready
    if (ret) {
        pr_err("VCAM: Failed to init vb2 queue\n");
        return ret;
    }

    /* register into video_device */
    vcam->vdev.queue = q;
    vcam->vdev.v4l2_dev = &vcam->v4l2_dev;    // set Parent
    vcam->vdev.fops = &vcam_fops;             // set file operations
    vcam->vdev.release = vcam_video_release;
    vcam->vdev.ioctl_ops = &vcam_ioctl_ops; // set ioctl operations
    vcam->vdev.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;


    /* regist control handler */
    v4l2_ctrl_handler_init(&vcam->ctrl_handler, 1);

    // Allocate and initialize a new standard V4L2 non-menu control.
    v4l2_ctrl_new_std(&vcam->ctrl_handler, &vcam_ctrl_ops,
                      V4L2_CID_EXPOSURE, 0, 255, 1, 128);

    /* initial exposure data */
    vcam->exposure = 128;

    // regist handler to v4l2_device
    vcam->v4l2_dev.ctrl_handler = &vcam->ctrl_handler;
    vcam->vdev.ctrl_handler = &vcam->ctrl_handler;

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
    v4l2_ctrl_handler_free(&vcam->ctrl_handler);
}