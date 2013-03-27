#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <linux/videodev2.h>

#define BUF_NUM 3
typedef struct __BUFINFO {
    int size;
    int line_size;
    int index;
    char *buf;
    char *rgbbuf;
}BUF_INFO;

BUF_INFO capbuf[BUF_NUM];
static int sockfd = 0;
struct sockaddr_in addr_remote;
#define PORT  5000

struct LCD_INFO {
    char *mem;
    int linesize;
    int size;
}; 
extern struct LCD_INFO lcdinfo;

char xxx[1024 * 768];

void dump_char(unsigned char *buf, int num)
{
    int i;

    for (i = 0; i < num; i++) {
        printf("%02x ", buf[i]);
    }
    printf("\n");
}

#if 1
void process(BUF_INFO *info)
{
	int fd;
	printf("now open testyuv.raw\n");
	fd = open("/mnt/testyuv.raw", O_RDWR | O_TRUNC | O_CREAT);
	if (fd == -1) {
		printf("open /mnt/testyuv.raw error\n");
		exit(-1);
	}

	printf("start to write testyuv.raw file\n");
	write(fd, info->buf, info->size);
	//write(fd, info->buf, 1024 * 1024);
	close(fd);
	printf("write file ok\n");
	//exit(0);
}

#endif

static int read_frame(int fd)
{
    struct v4l2_buffer buf;
    unsigned int i;

    memset((void*)&buf, 0, sizeof(buf));

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (-1 == ioctl(fd, VIDIOC_DQBUF, &buf)) {
        switch(errno) {
            case EAGAIN:
                return 0;

            case EIO:

            default:
                printf("VIDIOC_DQBUF error\n");
                exit(-1);
        }
    }

    //process image
    printf("capture buffers[%d] = {start = %p};\n",
            buf.index, capbuf[buf.index].buf);
	process(&capbuf[buf.index]);
	
    if (-1 == ioctl(fd, VIDIOC_QBUF, &buf)) {
        printf("VIDIOC_QBUF error\n");
        exit(-1);
    }
    return 1;
}
static void mainloop (int fd)
{
    unsigned int count;
    count = 1;

    while(count > 0) {
		if (1) {
            fd_set fds;
            struct timeval tv;
            int r;

            FD_ZERO(&fds);
            FD_SET(fd, &fds);

            tv.tv_sec = 2;
            tv.tv_usec = 0;

            r = select(fd + 1, &fds, NULL, NULL, &tv);
            
            if ( -1 == r) {
                if (EINTR == errno)
                    continue;
                printf("slect error\n");
                exit(-1);
            }

            if (0 == r) {
                printf("select time out ...\n");
                exit(EXIT_FAILURE);
            }
            read_frame(fd);
        }
        count--;
    }
}

int print_int(int data)
{
    unsigned char *p = (unsigned char *)&data;
    printf("%c%c%c%c\n", p[0], p[1], p[2], p[3]);

    return 0;
}
#define DEV_NAME "/dev/video0"
int main()
{
    int fd;
    struct v4l2_capability cap;
    struct v4l2_input input;
    struct v4l2_requestbuffers reqbuf;
    int i = 0;
    int index;
    int ret;

    fd = open(DEV_NAME, O_RDWR);
    if (fd == -1) {
        printf("open %s error\n", DEV_NAME);
        exit(-1);
    }
    printf("open %s ok ...\n", DEV_NAME);

   // lcd_open();
    if (-1 == ioctl(fd, VIDIOC_QUERYCAP, &cap)) {
        if (EINVAL == errno) {
            printf("%s is no v4l2 device\n", DEV_NAME);
            exit(-1);
        } else {
            printf("VIDIOC_QUERYCAP error\n");
            exit(-1);
        }
    }

    printf("ioctl ok VIDIOC_QUERYCAP ...\n");
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        printf("%s is no video capture device and capabilities is 0x%08x\n",
                DEV_NAME, cap.capabilities);
        exit(-1);
    }
    printf("%s is a capture device and opened ok...\n", DEV_NAME);

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        printf("%s does not support mmap ..\n", DEV_NAME);
        exit(-1);
    }

    printf("%s will use mmap ...\n",DEV_NAME);
    //获取输入设备信息
#if 0
    i = 0;
    memset(&input, 0, sizeof(input));
    while(1) {
        input.index = i;
        ret = ioctl(fd, VIDIOC_ENUMINPUT, &input);
        if (ret < 0) 
            break;

        printf("name = %s\n", input.name);
        i++;
    }
#endif 
    //设置输入设备为S-Video
#if 1
    index = 0;
    ret = ioctl(fd, VIDIOC_S_INPUT, &index);
    if (ret < 0) {
        printf("ioctl VIDIOC_S_INPUT error\n");
        close(fd);
        exit(-1);
    }
#endif

    input.index = index;
    ret = ioctl(fd, VIDIOC_ENUMINPUT, &input);
    if (ret < 0) {
        printf("ioctl VIDIOC_ENUMINPUT error\n");
        close(fd);
        exit(-1);
    }
    printf("curent input name = %s\n", input.name);

    struct v4l2_standard standard;
#if 0
    struct v4l2_fmtdesc fmt;
    i = 0;
    while(1) {
        fmt.index = i;
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ret = ioctl(fd, VIDIOC_ENUM_FMT, &fmt);
        if (ret < 0)  {
            break;
        }

        printf("description = %s\t\t", fmt.description);
        print_int(fmt.pixelformat);
     
        i++;
    }
	sleep(1);
#endif

	sleep(1);
    struct v4l2_format fmt1;
	memset(&fmt1, 0, sizeof(fmt1));	
    fmt1.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt1.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
    fmt1.fmt.pix.width = 1600;
    fmt1.fmt.pix.height = 1200;
    //fmt1.fmt.pix.field = V4L2_FIELD_INTERLACED;
	fmt1.fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;

    ret = ioctl(fd, VIDIOC_S_FMT, &fmt1);
    if (ret < 0) {
        printf("%s VIDIOC_S_FMT error\n");
        close(fd);
        exit(-1);
    }
    printf("set vidioc_s_fmt ok ....\n");
	sleep(1);
#if 0
	//add for samsung fimc
	struct v4l2_format sam_v4l2_fmt;
	struct v4l2_pix_format sam_pixfmt;
	memset(&sam_pixfmt, 0, sizeof(sam_pixfmt));
	sam_v4l2_fmt.type = V4L2_BUF_TYPE_PRIVATE;
	sam_pixfmt.width = 1600;
	sam_pixfmt.height = 1200;
	sam_pixfmt.pixelformat = V4L2_PIX_FMT_JPEG;
	sam_pixfmt.field = 2;
	sam_v4l2_fmt.fmt.pix = sam_pixfmt;

	if (ioctl(fd, VIDIOC_S_FMT, &sam_v4l2_fmt) < 0) {
		printf("canot s_fmt private ...for samsung\n");
		return -1;
	}
#endif

    reqbuf.count = 1;
    reqbuf.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    ret = ioctl(fd, VIDIOC_REQBUFS, &reqbuf);
    if (ret < 0) {
        printf("cannot allocate memory\n");
        close(fd);
        return -1;
    }
    printf("number of buffers allocated %d\n", reqbuf.count);

	sleep(1);
    //get physical address
    struct v4l2_buffer buffer;
    
	for (i = 0; i < reqbuf.count; i++) {
		buffer.index = i;
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buffer.memory = V4L2_MEMORY_MMAP;
		if (ioctl(fd, VIDIOC_QUERYBUF, &buffer) < -1) {
			printf("buffer VIDIOC_QUERYBUF error.\n");
			close(fd);
			exit(-1);
		}
		printf("querybuf ...\n");
		sleep(1);
		capbuf[i].buf = (char *)mmap(NULL, buffer.length, 
				PROT_READ | PROT_WRITE,
				MAP_SHARED, 
				fd,
				buffer.m.offset);
		if (capbuf[i].buf == NULL) {
			printf("mmap error \n");
			close(fd);
			exit(-1);
		}
		capbuf[i].size = buffer.length - 16;
		capbuf[i].line_size = fmt1.fmt.pix.bytesperline;
		sleep(1);
		printf("----------->buffer %d is ok. size if %d, add=%p\n", i, buffer.length, capbuf[i].buf);
		memset(capbuf[i].buf, 0x00, capbuf[i].size);
	}

	printf("now q buf ..\n");
	sleep(1);
    for (i = 0; i < 1; i++) {
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;
        if (-1 == ioctl(fd, VIDIOC_QBUF, &buffer)) {
            printf("VIDIOC_QBUF error\n");
            exit(-1);
        }
    }
	printf("now stream on\n");

	sleep(1);
    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == ioctl(fd, VIDIOC_STREAMON, &type)) {
        printf("VIDIOC_STREAMON error\n");
        exit(-1);
    }

    printf("video %s stream on ...\n", DEV_NAME);

    mainloop(fd);

    close(fd);
}
