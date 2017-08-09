#define _USE_MATH_DEFINES
#include "readFile.h"
#include "opencv2/opencv.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <omp.h>
#define ROW 494//1010
#define COL 505 //988
#define IMG_NUM 495 //990
#define H 13 // H + 1
#define DIRNUM 12
#define DIRNUMSQU 122 // DIRNUM * DIRNUM - (2 * DIRNUM-1)
#define JTHRESHOLD -8.3
using namespace cv;
using namespace std;

typedef vector < Vec3f > directionSet;

int h3 = H*H*H;
int h2 = H*H;
int s = 3;
int t = 4;

string fileLocate = "C:\\Users\\kimhyeji\\Desktop\\UCI\\project\\Pramook_black_velvet_3.03um_80kV_TIFF\\";



/*
open image files and store to 3D volume with Image Data
*/
void makeVolumeWithImage(vector<Mat> &v) {
	for (int i = 1; i <= IMG_NUM; i++) {
		Mat img;
		string image = fileLocate + format("%04d.tiff", i);
		//img = imread(image, CV_LOAD_IMAGE_ANYCOLOR);
		img = imread(image, CV_LOAD_IMAGE_UNCHANGED);
		normalize(img, img, 0.0, 1, CV_MINMAX, CV_32F);
		threshold(img, img, 0.64, 1, THRESH_BINARY_INV);
		img.convertTo(img, CV_8U, 255, 0);
		v[i - 1] = img;
		imshow("img", img);
		waitKey(0);
		img.release();
	}
}

/*
open image files and store to 3D volume
*/
void makeVolume(vector<Mat> &v) {

	FILE    *fp_sour;
	unsigned char buff[48]; //48byte
	size_t   n_size;

	fp_sour = fopen((fileLocate + "Pramook_black_velvet_3.03um_80kV_down.vol").c_str(), "rb");
	n_size = fread(buff, 1, 48, fp_sour);

	readHeader(buff);

	vector<float> data((((long long)sx)*sy)*sz);
	readData(data, fp_sour);

	//printData(data);

	fclose(fp_sour);
	for (int i = 0; i < IMG_NUM; i++) {
		Mat img = Mat_<float>(ROW, COL);

		for (int y = 0; y < ROW; y++) {
			for (int x = 0; x < COL; x++) {
			img.at<float>(y, x) = findData(data, { x,y,i });
			}
		}
		threshold(img, img, 0.55, 1, THRESH_BINARY_INV);
		img.convertTo(img, CV_8U, 255, 0);
		v[i] = img;
		img.release();
	}
}

/*
get rotation matrix
z,y,x
using spherical coordinate angles(theta, delta)
t = 2PAI - theta
d = delta - 1/2*PAI 
*/
Mat rotationMatrix(float d, float cosT) {
	float sinT = sin(acos(cosT));

	Mat XR = (Mat_<float>(3, 3) <<
		cosT,0, -sinT,
		0, 1, 0,
		sinT, 0, cosT
		);
	Mat ZR = (Mat_<float>(3, 3) <<
		1, 0, 0,
		0, sin(d), cos(d),
		0, -cos(d), sin(d)
		);
	return ZR*XR;
}

/*
make set of direction vectors
*/
void makedirectionSet(directionSet &v, vector<Mat> &R) {
	float M_PI2 = M_PI * 2;
	float intervalD = M_PI2 / (DIRNUM - 1);
	float intervalZ = 2.0 / (DIRNUM - 1);

	int count = 0;
	for (float z = -1.0; z <= 1.0; z += intervalZ) {
		for (float d = 0; d <= M_PI2; d += intervalD) {
			float y = sqrt(1.0 - pow(z, 2)) * cos(d);
			float x = sqrt(1.0 - pow(z, 2)) * sin(d);
			if (abs(z) == 1 && d != 0) continue;
			R[count] = rotationMatrix(d, z);
			v[count++] = Vec3f(z, y, x);
		}
	}
}

/*
get distance with direction vector and location p
*/
float getDistance(const Vec3f &direct, const Vec3f &x) {
	Vec3f dist = x - (x.dot(direct) * direct);
	return sqrt(pow(dist[0], 2) + pow(dist[1], 2) + pow(dist[2], 2));
}


/*
save q function values
*/
void saveDistance(float(&saveData)[H*H*H], const directionSet &d, vector<Mat> &R) {
	int h_half = H / 2;

	for (int p_z = 0; p_z < H; p_z++) {
		for (int p_y = 0; p_y < H; p_y++) {
			for (int p_x = 0; p_x < H; p_x++) {
				float dist = getDistance(Vec3f(1, 0, 0), Vec3f(p_z - h_half, p_y - h_half, p_x - h_half));
				float pow_dist = dist * dist;
				float q_func = -2 * exp(-s * pow_dist) + exp(-t * pow_dist);
				saveData[p_z*h2 + p_y*H + p_x] = q_func;
			}
		}
	}
}

/*
testing code for making J value images.
*/
float getJvalue(const directionSet &d, const vector<Mat> &volume, const Vec3i &v, const float(&q)[H * H * H], bool density, vector<Mat> &R) {
	int z = v[0];
	int y = v[1];
	int x = v[2];
	float maxValue = -99999;
	int h_half = H / 2;

	Vec3f result;
	for (int i = 0; i < DIRNUMSQU; i++) {
		float j = 0;
		for (int p_z = 0; p_z < H; p_z++) {
			if (z + p_z >= IMG_NUM) continue;
			for (int p_y = 0; p_y < H; p_y++) {
				if (y + p_y >= ROW) continue;
				for (int p_x = 0; p_x < H; p_x++) {
					if (x + p_x >= COL) continue;
					Mat x_vec = (Mat_<float>(3, 1) <<
						p_z - h_half, p_y - h_half, p_x - h_half);
					Mat p = R[i] * x_vec;
					int v_z = int(z + p.at<float>(0, 0) + 0.5);
					int v_y = int(y + p.at<float>(1, 0) + 0.5);
					int v_x = int(x + p.at<float>(2, 0) + 0.5);

					if (v_z < 0) continue;
					if (volume[v_z].at<uchar>(v_y, v_x) > 0) {
						float q_func = q[p_z*h2 + p_y*H + p_x];
						j += q_func;
					}

				}
			}
		}
		// get maximum of J values
		if (maxValue < j) {
			maxValue = j;
			result = d[i];
		}
	}

	density = (maxValue > 0.001) ? 1 : 0;
	return maxValue;
}

/*
get Maximum convolution value
*/
Vec3f getMaxConvolution(const directionSet &d, const vector<Mat> &volume, const Vec3i &v, const float(&q)[H * H * H], bool &density, vector<Mat> &R) {
	int z = v[0];
	int y = v[1];
	int x = v[2];
	float maxValue = -9999;
	int h_half = H / 2;

	Vec3f result;
	for (int i = 0; i < DIRNUMSQU; i++) {
		float j = 0;
		for (int p_z = 0; p_z < H; p_z++) {
			if (z + p_z >= IMG_NUM) continue;
			for (int p_y = 0; p_y < H; p_y++) {
				if (y + p_y >= ROW) continue;
				for (int p_x = 0; p_x < H; p_x++) {
					if (x + p_x >= COL) continue;
					Mat x_vec = (Mat_<float>(3, 1) <<
						p_z - h_half, p_y - h_half, p_x - h_half);
					Mat p = R[i] * x_vec;
					int v_z = int(z + p.at<float>(0, 0) + 0.5);
					int v_y = int(y + p.at<float>(1, 0) + 0.5);
					int v_x = int(x + p.at<float>(2, 0) + 0.5);

					if (v_z < 0) continue;
					if (volume[v_z].at<uchar>(v_y, v_x) > 0) {
						float q_func = q[p_z*h2 + p_y*H + p_x];
						j += q_func;
					}
				}
			}
		}
		// get maximum of J values
		if (maxValue < j) {
			maxValue = j;
			result = d[i];
		}
	}
	if (maxValue < JTHRESHOLD) {
		return Vec3f(0, 0, 0);
	}
	return result;
}

/*
check keyboard input,
change the J threshold
*/
void onChange(int pos, void *user){

	Mat image = ((Mat*)user)[0];
	Mat test(ROW, COL, CV_8UC1);
	float threshold = pos * -0.1;
	for (int i = 0; i < 494; i++) {
		for (int j = 0; j < 505; j++) {
			if (image.at<float>(i, j) > threshold)
				test.at<uchar>(i, j) = 255;
			else test.at<uchar>(i, j) = 0;
		}
	}
	cout << threshold << endl;
	imshow("image", test);
}

/*
set J threshold with ,(-) .(+) keys
threshold = pos * -0.1
-8.2
*/
void setThreshold(){

	Mat image = imread(fileLocate + "test.exr", CV_LOAD_IMAGE_UNCHANGED);
	int pos = 100;

	imshow("image", image);
	onChange(pos, (void *)&image);
	createTrackbar("threshold", "image", &pos, 255, onChange, (void*)&image);
	while (1) {
		char Key = (char)waitKey();
		if (Key == 27) break;
		switch (Key) {
		case ',':
			pos--;
			onChange(pos, (void *)&image);
			setTrackbarPos("threshold","image", pos);
			break;
		case '.':
			pos++;
			onChange(pos, (void *)&image);
			setTrackbarPos("threshold", "image", pos);
			break;
		}
		if (pos < 0 || pos > 255) pos = 0;
	}

}

int main(int argc, char **argv) {

	vector<Mat> volumeData(IMG_NUM);
	vector<Mat> R(DIRNUMSQU, Mat(3, 3, CV_32FC1));
	float qfuncData[H*H*H];
	makeVolume(volumeData);

	directionSet dicVec(DIRNUMSQU);
	makedirectionSet(dicVec, R);
	saveDistance(qfuncData, dicVec, R);

	/*
	recover the orientation field
	*/
	for (int z = 100; z < IMG_NUM; z++) {
		cout << z << endl;
		Mat image_dir(ROW, COL, CV_32FC3);
		//Mat image_den(ROW, COL, CV_32FC1);
		bool density = 0;

		#pragma omp parallel for schedule(dynamic,6)
		for (int y = 0; y < ROW; y++) {
			for (int x = 0; x < COL; x++) {
				if (volumeData[z].at<uchar>(y, x) > 0) {
					image_dir.at<Vec3f>(y, x)[0] = 0;
					image_dir.at<Vec3f>(y, x)[1] = 0;
					image_dir.at<Vec3f>(y, x)[2] = 0;
					continue;
				}
				Vec3f result = getMaxConvolution(dicVec, volumeData, Vec3i(z, y, x), qfuncData, density, R);
				
				image_dir.at<Vec3f>(y, x)[0] = result[1] * 255;//b - x(col)
				image_dir.at<Vec3f>(y, x)[1] = result[2] * 255;//g - y(row)
				image_dir.at<Vec3f>(y, x)[2] = result[0] * 255;//r - z(depth)
								
				/*
				J threshold test
				*/
				//image_den.at<float>(y,x) = getJvalue(dicVec, volumeData, Vec3i(z, y, x), qfuncData, density, R);
			}

			if (y % 10 == 0) {
				cout << y << endl;
				imwrite("-8.3 " + format("%d.jpg", z), image_dir);
			}
		}
		image_dir.release();
	}

}
