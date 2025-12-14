#include <linux/module.h>
#include <linux/init.h>


static int __init vcam_init(void)
{
    int ret;

    pr_info("VCAM: Initializing Virtual Camera Module\n");

    /* Initialization code for the virtual camera module goes here */

    pr_info("VCAM: Virtual Camera Module initialized successfully\n");
    return 0;
}

static void __exit vcam_exit(void)
{
    pr_info("VCAM: Exiting Virtual Camera Module\n");

    /* Cleanup code for the virtual camera module goes here */

    pr_info("VCAM: Virtual Camera Module exited successfully\n");
}

module_init(vcam_init);
module_exit(vcam_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Brian");
MODULE_DESCRIPTION("Virtual V4L2 compatible camera device driver");

