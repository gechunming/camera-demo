
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
#include <sys/poll.h>

struct sec_cam_parm {
	struct v4l2_captureparm capture;
	int contrast;
	int effects;
	int brightness;
	int flash_mode;
	int focus_mode;
	int iso;
	int metering;
	int saturation;
	int scene_mode;
	int sharpness;
	int white_balance;
};
struct fimc_buffer {
    void    *start;
    size_t  length;
};

#define CAMERA_DEV_NAME   "/dev/video0"
#define CAMERA_DEV_NAME2   "/dev/video2"
#define V4L2_CID_CAMERA_CHECK_DATALINE		(V4L2_CID_PRIVATE_BASE + 112)
#define V4L2_CID_CAMERA_RETURN_FOCUS		(V4L2_CID_PRIVATE_BASE + 119)
#define V4L2_CID_CAM_JPEG_MAIN_SIZE		(V4L2_CID_PRIVATE_BASE + 32)
#define V4L2_CID_STREAM_PAUSE			(V4L2_CID_PRIVATE_BASE + 53)
#define V4L2_CID_CAM_JPEG_MAIN_OFFSET		(V4L2_CID_PRIVATE_BASE + 33)
#define V4L2_CID_CAM_JPEG_THUMB_SIZE		(V4L2_CID_PRIVATE_BASE + 34)
#define V4L2_CID_CAM_JPEG_THUMB_OFFSET		(V4L2_CID_PRIVATE_BASE + 35)
#define V4L2_CID_CAM_JPEG_POSTVIEW_OFFSET	(V4L2_CID_PRIVATE_BASE + 36)
#define V4L2_CID_PADDR_Y		(V4L2_CID_PRIVATE_BASE + 1)
#define MAX_BUFFERS 9

#define VIDEO_COMMENT_MARKER_H          0xFFBE
#define VIDEO_COMMENT_MARKER_L          0xFFBF
#define VIDEO_COMMENT_MARKER_LENGTH     4
#define JPEG_EOI_MARKER                 0xFFD9
#define HIBYTE(x) (((x) >> 8) & 0xFF)
#define LOBYTE(x) ((x) & 0xFF)
int m_cam_fd;
int m_cam_fd2;
int m_preview_v4lformat = V4L2_PIX_FMT_YUV420;
int m_preview_width = 720;
int m_preview_height = 480;
int m_chk_dataline = 0;
int             m_postview_offset;
struct pollfd   m_events_c;
struct v4l2_streamparm m_streamparm;
int m_snapshot_v4lformat = V4L2_PIX_FMT_VYUY;
int m_snapshot_width = 2560;
int m_snapshot_height = 1920;
struct fimc_buffer m_capture_buf;

static int fimc_v4l2_querycap(int fp)
{
    struct v4l2_capability cap;
    int ret = 0;

    ret = ioctl(fp, VIDIOC_QUERYCAP, &cap);

    if (ret < 0) {
        printf("ERR(%s):VIDIOC_QUERYCAP failed\n", __func__);
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        printf("ERR(%s):no capture devices\n", __func__);
        return -1;
    }

    return ret;
}
static const unsigned char * fimc_v4l2_enuminput(int fp, int index)
{
    static struct v4l2_input input;

    input.index = index;
    if (ioctl(fp, VIDIOC_ENUMINPUT, &input) != 0) {
        printf("ERR(%s):No matching index found\n", __func__);
        return NULL;
    }
    printf("Name of input channel[%d] is %s\n", input.index, input.name);

    return input.name;
}

static int get_pixel_depth(unsigned int fmt)
{
    int depth = 0;

    switch (fmt) {
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_YVU420:
          depth = 12;
        break;

    case V4L2_PIX_FMT_RGB565:
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_YVYU:
    case V4L2_PIX_FMT_UYVY:
    case V4L2_PIX_FMT_VYUY:
    case V4L2_PIX_FMT_NV16:
    case V4L2_PIX_FMT_NV61:
    case V4L2_PIX_FMT_YUV422P:
        depth = 16;
        break;

    case V4L2_PIX_FMT_RGB32:
        depth = 32;
        break;
    }

    return depth;
}
static int fimc_v4l2_s_input(int fp, int index)
{
    struct v4l2_input input;
    int ret;

    input.index = index;

    ret = ioctl(fp, VIDIOC_S_INPUT, &input);
    if (ret < 0) {
        printf("ERR(%s):VIDIOC_S_INPUT failed\n", __func__);
        return ret;
    }

    return ret;
}

static int fimc_v4l2_streamoff(int fp)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int ret;

    printf("%s :", __func__);
    ret = ioctl(fp, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        printf("ERR(%s):VIDIOC_STREAMOFF failed\n", __func__);
        return ret;
    }

    return ret;
}
static int fimc_v4l2_g_ctrl(int fp, unsigned int id)
{
    struct v4l2_control ctrl;
    int ret;

    ctrl.id = id;

    ret = ioctl(fp, VIDIOC_G_CTRL, &ctrl);
    if (ret < 0) {
        printf("ERR(%s): VIDIOC_G_CTRL(id = 0x%x (%d)) failed, ret = %d\n",
             __func__, id, id-V4L2_CID_PRIVATE_BASE, ret);
        return ret;
    }

    return ctrl.value;
}
static int fimc_v4l2_s_fmt(int fp, int width, int height, unsigned int fmt, int flag_capture)
{
    struct v4l2_format v4l2_fmt;
    struct v4l2_pix_format pixfmt;
    int ret;

    memset(&v4l2_fmt, 0, sizeof(struct v4l2_format));
    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    memset(&pixfmt, 0, sizeof(pixfmt));

    pixfmt.width = width;
    pixfmt.height = height;
    pixfmt.pixelformat = fmt;

    pixfmt.sizeimage = (width * height * get_pixel_depth(fmt)) / 8;

    pixfmt.field = V4L2_FIELD_NONE;

    v4l2_fmt.fmt.pix = pixfmt;

    /* Set up for capture */
    ret = ioctl(fp, VIDIOC_S_FMT, &v4l2_fmt);
    if (ret < 0) {
        printf("ERR(%s):VIDIOC_S_FMT failed\n", __func__);
        return -1;
    }

    return 0;
}

static int fimc_v4l2_enum_fmt(int fp, unsigned int fmt)
{
    struct v4l2_fmtdesc fmtdesc;
    int found = 0;

    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmtdesc.index = 0;

    while (ioctl(fp, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        if (fmtdesc.pixelformat == fmt) {
            printf("passed fmt = %#x found pixel format[%d]: %s\n", fmt, fmtdesc.index, fmtdesc.description);
            found = 1;
            break;
        }

        fmtdesc.index++;
    }

    if (!found) {
        printf("unsupported pixel format\n");
        return -1;
    }

    return 0;
}

static int fimc_v4l2_reqbufs(int fp, enum v4l2_buf_type type, int nr_bufs)
{
    struct v4l2_requestbuffers req;
    int ret;

    req.count = nr_bufs;
    req.type = type;
    req.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(fp, VIDIOC_REQBUFS, &req);
    if (ret < 0) {
        printf("ERR(%s):VIDIOC_REQBUFS failed\n", __func__);
        return -1;
    }

    return req.count;
}

static int fimc_v4l2_s_ctrl(int fp, unsigned int id, unsigned int value)
{
    struct v4l2_control ctrl;
    int ret;

    ctrl.id = id;
    ctrl.value = value;

    ret = ioctl(fp, VIDIOC_S_CTRL, &ctrl);
    if (ret < 0) {
        printf("ERR(%s):VIDIOC_S_CTRL(id = %#x (%d), value = %d) failed ret = %d\n",
             __func__, id, id-V4L2_CID_PRIVATE_BASE, value, ret);

        return ret;
    }

    return ctrl.value;
}

static int fimc_v4l2_streamon(int fp)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int ret;

    ret = ioctl(fp, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        printf("ERR(%s):VIDIOC_STREAMON failed\n", __func__);
        return ret;
    }

    return ret;
}

static int fimc_v4l2_qbuf(int fp, int index)
{
    struct v4l2_buffer v4l2_buf;
    int ret;

    v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_buf.memory = V4L2_MEMORY_MMAP;
    v4l2_buf.index = index;

    ret = ioctl(fp, VIDIOC_QBUF, &v4l2_buf);
    if (ret < 0) {
        printf("ERR(%s):VIDIOC_QBUF failed\n", __func__);
        return ret;
    }

    return 0;
}

static int fimc_v4l2_s_parm(int fp, struct v4l2_streamparm *streamparm)
{
    int ret;

    streamparm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = ioctl(fp, VIDIOC_S_PARM, streamparm);
    if (ret < 0) {
        printf("ERR(%s):VIDIOC_S_PARM failed\n", __func__);
        return ret;
    }

    return 0;
}

static int fimc_poll(struct pollfd *events)
{
    int ret;

    /* 10 second delay is because sensor can take a long time
     * to do auto focus and capture in dark settings
     */
    ret = poll(events, 1, 10000);
    if (ret < 0) {
        printf("ERR(%s):poll error\n", __func__);
        return ret;
    }

    if (ret == 0) {
        printf("ERR(%s):No data in 10 secs..\n", __func__);
        return ret;
    }

    return ret;
}

unsigned int getPhyAddrY(int index)
{
    unsigned int addr_y;

    addr_y = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_PADDR_Y, index);
    return addr_y;
}

int jpeg_lenght = 0;
static int fimc_v4l2_querybuf(int fp, struct fimc_buffer *buffer, enum v4l2_buf_type type)
{
    struct v4l2_buffer v4l2_buf;
    int ret;

    printf("%s :", __func__);

    v4l2_buf.type = type;
    v4l2_buf.memory = V4L2_MEMORY_MMAP;
    v4l2_buf.index = 0;

    ret = ioctl(fp , VIDIOC_QUERYBUF, &v4l2_buf);
    if (ret < 0) {
        printf("ERR(%s):VIDIOC_QUERYBUF failed\n", __func__);
        return -1;
    }

    buffer->length = v4l2_buf.length;
	jpeg_lenght = buffer->length;
    if ((buffer->start = (char *)mmap(0, v4l2_buf.length,
                                         PROT_READ | PROT_WRITE, MAP_SHARED,
                                         fp, v4l2_buf.m.offset)) < 0) {
         printf("%s %d] mmap() failed\n",__func__, __LINE__);
         return -1;
    }
	memset(buffer->start, 0x00, buffer->length-10);
    printf("%s: buffer->start = %p v4l2_buf.length = %d",
         __func__, buffer->start, v4l2_buf.length);

    return 0;
}
static int fimc_v4l2_s_fmt_cap(int fp, int width, int height, unsigned int fmt)
{
    struct v4l2_format v4l2_fmt;
    struct v4l2_pix_format pixfmt;
    int ret;

    memset(&pixfmt, 0, sizeof(pixfmt));

    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    pixfmt.width = width;
    pixfmt.height = height;
    pixfmt.pixelformat = fmt;
    if (fmt == V4L2_PIX_FMT_JPEG) {
        pixfmt.colorspace = V4L2_COLORSPACE_JPEG;
    }

    pixfmt.sizeimage = (width * height * get_pixel_depth(fmt)) / 8;

    v4l2_fmt.fmt.pix = pixfmt;

    //printf("ori_w %d, ori_h %d, w %d, h %d\n", width, height, v4l2_fmt.fmt.pix.width, v4l2_fmt.fmt.pix.height);

    /* Set up for capture */
    ret = ioctl(fp, VIDIOC_S_FMT, &v4l2_fmt);
    if (ret < 0) {
        printf("ERR(%s):VIDIOC_S_FMT failed\n", __func__);
        return ret;
    }

    return ret;
}
static int fimc_v4l2_dqbuf(int fp)
{
    struct v4l2_buffer v4l2_buf;
    int ret;

    v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_buf.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(fp, VIDIOC_DQBUF, &v4l2_buf);
    if (ret < 0) {
        printf("ERR(%s):VIDIOC_DQBUF failed, dropped frame\n", __func__);
        return ret;
    }

    return v4l2_buf.index;
}
int camera_init(int index)
{
	int ret;
    struct sec_cam_parm   *m_params;
    m_params = (struct sec_cam_parm*)&m_streamparm.parm.raw_data;
    m_params->capture.timeperframe.numerator = 1;
    m_params->capture.timeperframe.denominator = 0;
    m_params->contrast = -1;
    m_params->effects = -1;
    m_params->brightness = -1;
    m_params->flash_mode = -1;
    m_params->focus_mode = -1;
    m_params->iso = -1;
    m_params->metering = -1;
    m_params->saturation = -1;
    m_params->scene_mode = -1;
    m_params->sharpness = -1;
    m_params->white_balance = -1;

	m_cam_fd = open(CAMERA_DEV_NAME, O_RDWR);
	if (m_cam_fd < 0) {
		printf("open %s error\n", CAMERA_DEV_NAME);
		return -1;
	}
	ret = fimc_v4l2_querycap(m_cam_fd);
	if (ret < 0) {
		printf("querycap error\n");
		return -1;
	}
	if (!fimc_v4l2_enuminput(m_cam_fd, index)) {
		printf("enum init error\n");
		return -1;
	}
	ret = fimc_v4l2_s_input(m_cam_fd, index);
	if (ret < 0) {
		printf("s_input error\n");
		return -1;
	}

	m_cam_fd2 = open(CAMERA_DEV_NAME2, O_RDWR);
	if (m_cam_fd2 < 0) {
		printf("open %s error\n", CAMERA_DEV_NAME2);
		return -1;
	}

	ret = fimc_v4l2_querycap(m_cam_fd2);
	if (ret < 0) {
		printf("querycap camera2 error\n");
		return -1;
	}
	if (!fimc_v4l2_enuminput(m_cam_fd2, index)) {
		printf("enuminpuyt camera2 error\n");
		return -1;
	}
	ret = fimc_v4l2_s_input(m_cam_fd2, index);
	if (ret < 0)  {
		printf("s_input camera2 error\n");
		return -1;
	}
	return 0;
}

int camera_startPreview(void)
{
	int i;
    struct v4l2_streamparm streamparm;
    struct sec_cam_parm *parms;
    parms = (struct sec_cam_parm*)&streamparm.parm.raw_data;
    printf("%s :", __func__);

    // aleady started
    if (m_cam_fd <= 0) {
        printf("ERR(%s):Camera was closed\n", __func__);
        return -1;
    }

    memset(&m_events_c, 0, sizeof(m_events_c));
    m_events_c.fd = m_cam_fd;
    m_events_c.events = POLLIN | POLLERR;

    /* enum_fmt, s_fmt sample */
    int ret = fimc_v4l2_enum_fmt(m_cam_fd, m_preview_v4lformat);
	if (ret < 0) {
		printf("enum_fmt error\n");
		return -1;
	}
	printf("m_preview_v4lformat=%d\n", m_preview_v4lformat);
    ret = fimc_v4l2_s_fmt(m_cam_fd, m_preview_width,m_preview_height,m_preview_v4lformat, 0);
	if (ret < 0) {
		printf("s_fmt error\n");
		return -1;
	}

    ret = fimc_v4l2_reqbufs(m_cam_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, MAX_BUFFERS);
	if (ret < 0) {
		printf("reqbufs error\n");
		return -1;
	}

		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_CHECK_DATALINE, m_chk_dataline);
		if (ret < 0) {
			printf("s_ctrl error\n");
			return -1;
		}

    /* start with all buffers in queue */
    for (i = 0; i < MAX_BUFFERS; i++) {
        ret = fimc_v4l2_qbuf(m_cam_fd, i);
		if (ret < 0) {
			printf("v4l2_qbuf error\n");
			return -1;
		}
    }
#if (BACK_CAMERA_FLAG_HW_UPDOWN_MIRROR == TRUE)
//	setHorizontalMirror(m_cam_fd);
#endif

    ret = fimc_v4l2_streamon(m_cam_fd);
	if (ret < 0) {
		printf("stream on error\n");
		return -1;
	}

    ret = fimc_v4l2_s_parm(m_cam_fd, &m_streamparm);
	if (ret < 0) {
		printf("s_parm error\n");
		return -1;
	}

    // It is a delay for a new frame, not to show the previous bigger ugly picture frame.
    ret = fimc_poll(&m_events_c);
	if (ret < 0) {
		printf("fimc_poll error\n");
		return -1;
	}
    ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_RETURN_FOCUS, 0);
	if (ret < 0) {
		printf("camera return focus error\n");
		return -1;
	}

    printf("%s: got the first frame of the preview\n", __func__);

    return 0;
}

int camera_setSnapshotCmd(void)
{
	int ret = 0;

    memset(&m_events_c, 0, sizeof(m_events_c));
    m_events_c.fd = m_cam_fd;
    m_events_c.events = POLLIN | POLLERR;
    int nframe = 1;

    ret = fimc_v4l2_enum_fmt(m_cam_fd,m_snapshot_v4lformat);
	if (ret < 0) {
		printf("%s %s %d error\n", __FILE__, __func__, __LINE__);
		return -1;
	}

	//设置像素参数
	printf("snapshot format =%d\n", m_snapshot_v4lformat);
    ret = fimc_v4l2_s_fmt_cap(m_cam_fd, m_snapshot_width, m_snapshot_height, V4L2_PIX_FMT_JPEG);
	if (ret < 0) {
		printf("%s %s %d error\n", __FILE__, __func__, __LINE__);
		return -1;
	}

	//请求缓冲
    ret = fimc_v4l2_reqbufs(m_cam_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, nframe);
	if (ret < 0) {
		printf("%s %s %d error\n", __FILE__, __func__, __LINE__);
		return -1;
	}

    ret = fimc_v4l2_querybuf(m_cam_fd, &m_capture_buf, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if (ret < 0) {
		printf("%s %s %d error\n", __FILE__, __func__, __LINE__);
		return -1;
	}

    ret = fimc_v4l2_qbuf(m_cam_fd, 0);
	if (ret < 0) {
		printf("%s %s %d error\n", __FILE__, __func__, __LINE__);
		return -1;
	}

    ret = fimc_v4l2_streamon(m_cam_fd);
	if (ret < 0) {
		printf("%s %s %d error\n", __FILE__, __func__, __LINE__);
		return -1;
	}
}

unsigned char *camera_getJpeg(int *jpeg_size, unsigned int *phyaddr)
{
    int index, ret = 0;
    unsigned char *addr;

    ret = fimc_poll(&m_events_c);
	if (ret < 0) {
		printf("%s %s %d\n", __FILE__, __func__, __LINE__);
		return NULL;
	}
    index = fimc_v4l2_dqbuf(m_cam_fd);
    if (index != 0) {
        printf("ERR(%s):wrong index = %d\n", __func__, index);
        return NULL;
    }

    *jpeg_size = fimc_v4l2_g_ctrl(m_cam_fd, V4L2_CID_CAM_JPEG_MAIN_SIZE);
	if (*jpeg_size < 0) {
		printf("%s %s %d\n", __FILE__, __func__, __LINE__);
		return NULL;
	}

    int main_offset = fimc_v4l2_g_ctrl(m_cam_fd, V4L2_CID_CAM_JPEG_MAIN_OFFSET);
	if (main_offset < 0) {
		printf("%s %s %d\n", __FILE__, __func__, __LINE__);
		return NULL;
	}
    m_postview_offset = fimc_v4l2_g_ctrl(m_cam_fd, V4L2_CID_CAM_JPEG_POSTVIEW_OFFSET);
	if (m_postview_offset < 0) {
		printf("%s %s %d\n", __FILE__, __func__, __LINE__);
		return NULL;
	}

    ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_STREAM_PAUSE, 0);
	if (ret < 0) {
		printf("%s %s %d\n", __FILE__, __func__, __LINE__);
		return NULL;
	}

    addr = (unsigned char*)(m_capture_buf.start) + main_offset;
    *phyaddr = getPhyAddrY(index) + m_postview_offset;

    ret = fimc_v4l2_streamoff(m_cam_fd);
	if (ret < 0) {
		printf("%s %s %d\n", __FILE__, __func__, __LINE__);
		return NULL;
	}

	return addr;
}

int camera_stopPreview()
{
	int ret;
    ret = fimc_v4l2_streamoff(m_cam_fd);
	if (ret < 0) {
		printf("%s %s %d\n", __FILE__, __func__, __LINE__);
		return -1;
	}
	return 0;
}

void process(unsigned char *addr, int len)
{
	int fd;
	printf("now open testyuv.raw\n");
	fd = open("/mnt/testyuv.raw", O_RDWR | O_TRUNC | O_CREAT);
	if (fd == -1) {
		printf("open /mnt/testyuv.raw error\n");
		exit(-1);
	}

	printf("start to write testyuv.raw file\n");
	write(fd, addr, len);
	//write(fd, info->buf, 1024 * 1024);
	close(fd);
	sync();
	sleep(1);
	printf("write file ok\n");
	//exit(0);
}

int main()
{
	int ret;
	int jpeg_size;
	unsigned int phyaddr;
	unsigned char *addr;

	ret = camera_init(0);
	if (ret < 0) {
		return 0;
	}
	ret = camera_startPreview();
	if (ret < 0) {
		return 0;
	}
	ret = camera_stopPreview();
	if (ret < 0) {
		return 0;
	}
	ret = camera_setSnapshotCmd();
	if (ret < 0) {
		return 0;
	}
	addr = camera_getJpeg(&jpeg_size, &phyaddr);
	printf("camera getJpeg ok, addr %p\n", addr);
	process(addr, jpeg_lenght-10);
}
