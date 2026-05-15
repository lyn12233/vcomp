#include "encode/encoder.h"
#include "encode/predictor.h"
#include "encode/quant.h"
#include "encode/search.h"
#include "encode/types.h"
#include "util/log.h"
#include "util/pixbuf.h"

#include <assert.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <ios>
#include <iostream>
#include <stdio.h>

#include <opencv2/core/core.hpp>
#include <opencv2/core/types_c.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/imgproc/types_c.h>
#include <opencv2/opencv.hpp>

using std::cout;
using std::endl;

void read_im(c1enc_frame_t*frm,const char*fn, c1enc_ctx_t*ctx,c1enc_option_t*opt){
    cv::Mat img=cv::imread(fn);
    cv::cvtColor(img, img, CV_RGB2BGR);
    c1_pixbuf_t pix = c1_pixbuf_from_ptr(C1_PIXBUF_C3I8, (uint16_t)img.size[0], (uint16_t)img.size[1], img.data);
    c1_pixbuf_t pix_yuv = c1_pixbuf_cvt_rgbi8_to_yuv16(&pix);
    c1_pixbuf_clear(&pix);
    c1enc_encode(frm,&pix_yuv,ctx,opt);
    c1_pixbuf_clear(&pix_yuv);

    pix=c1_pixbuf_cvt_yuv16_to_rgbi8(&frm->pix);
    img=cv::Mat(pix.h,pix.w,CV_8UC3);
    memcpy(img.data, pix.buf->ptr, pix.h * pix.w * sizeof(uint8_t) * 3);
    c1_pixbuf_clear(&pix);

    cv::cvtColor(img, img, CV_RGB2BGR);
    cv::imshow("RGB", img);   
    cv::waitKey();
}

int main(){
    std::ios_base::sync_with_stdio();
    c1enc_frame_t frm={0};
    c1enc_ctx_t ctx={0};
    c1_lookup_init_q_inf();
    c1enc_option_t opt={0};
    opt.max_p_frames=16;
    opt.qi_delta_max=8;
    opt.qp=50;
    opt.sample_qi_prescaler=1;
    read_im(&frm,"test/img/screen_content_1.png",&ctx,&opt);
    read_im(&frm,"test/img/screen_content_2.png",&ctx,&opt);
}