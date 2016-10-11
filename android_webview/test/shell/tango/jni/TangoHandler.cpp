/*
 * Copyright 2014 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cstdlib>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <cassert>

#include "TangoHandler.h"

namespace {

constexpr int kTangoCoreMinimumVersion = 9377;

void onTangoXYZijAvailable(void* context, const TangoXYZij* tangoXYZij)
{
	tango_chromium::TangoHandler::getInstance()->onTangoXYZijAvailable(tangoXYZij);
}

void onCameraFrameAvailable(void* context, TangoCameraId, const TangoImageBuffer* buffer) 
{
	tango_chromium::TangoHandler::getInstance()->onCameraFrameAvailable(buffer);
}

// We could do this conversion in a fragment shader if all we care about is
// rendering, but we show it here as an example of how people can use RGB data
// on the CPU.
inline void yuv2Rgb(uint8_t yValue, uint8_t uValue, uint8_t vValue, uint8_t* r,
                    uint8_t* g, uint8_t* b) {
  *r = yValue + (1.370705 * (vValue - 128));
  *g = yValue - (0.698001 * (vValue - 128)) - (0.337633 * (uValue - 128));
  *b = yValue + (1.732446 * (uValue - 128));
}

inline size_t closestPowerOfTwo(size_t value)
{
	size_t powerOfTwo = 2;
	while(value > powerOfTwo) powerOfTwo *= 2;
	return powerOfTwo;
}

inline void multiplyMatrixWithVector(const float* m, const double* v, double* vr, bool addTranslation = true) {
	double v0 = v[0];
	double v1 = v[1];
	double v2 = v[2];
    vr[0] = m[ 0] * v0 + m[ 4] * v1 + m[ 8] * v2 + (addTranslation ? m[12] : 0);
    vr[1] = m[ 1] * v0 + m[ 5] * v1 + m[ 9] * v2 + (addTranslation ? m[13] : 0);
    vr[2] = m[ 2] * v0 + m[ 6] * v1 + m[10] * v2 + (addTranslation ? m[14] : 0);
}

inline void matrixInverse(const float* m, float* o)
{
    // based on http://www.euclideanspace.com/maths/algebra/matrix/functions/inverse/fourD/index.htm
    float* te = o;
    const float* me = m;

    float n11 = me[ 0 ], n21 = me[ 1 ], n31 = me[ 2 ], n41 = me[ 3 ],
	    n12 = me[ 4 ], n22 = me[ 5 ], n32 = me[ 6 ], n42 = me[ 7 ],
	    n13 = me[ 8 ], n23 = me[ 9 ], n33 = me[ 10 ], n43 = me[ 11 ],
	    n14 = me[ 12 ], n24 = me[ 13 ], n34 = me[ 14 ], n44 = me[ 15 ],

	    t11 = n23 * n34 * n42 - n24 * n33 * n42 + n24 * n32 * n43 - n22 * n34 * n43 - n23 * n32 * n44 + n22 * n33 * n44,
	    t12 = n14 * n33 * n42 - n13 * n34 * n42 - n14 * n32 * n43 + n12 * n34 * n43 + n13 * n32 * n44 - n12 * n33 * n44,
	    t13 = n13 * n24 * n42 - n14 * n23 * n42 + n14 * n22 * n43 - n12 * n24 * n43 - n13 * n22 * n44 + n12 * n23 * n44,
	    t14 = n14 * n23 * n32 - n13 * n24 * n32 - n14 * n22 * n33 + n12 * n24 * n33 + n13 * n22 * n34 - n12 * n23 * n34;

    float det = n11 * t11 + n21 * t12 + n31 * t13 + n41 * t14;

    assert(det != 0);

    float detInv = 1.0 / det;

    te[ 0 ] = t11 * detInv;
    te[ 1 ] = ( n24 * n33 * n41 - n23 * n34 * n41 - n24 * n31 * n43 + n21 * n34 * n43 + n23 * n31 * n44 - n21 * n33 * n44 ) * detInv;
    te[ 2 ] = ( n22 * n34 * n41 - n24 * n32 * n41 + n24 * n31 * n42 - n21 * n34 * n42 - n22 * n31 * n44 + n21 * n32 * n44 ) * detInv;
    te[ 3 ] = ( n23 * n32 * n41 - n22 * n33 * n41 - n23 * n31 * n42 + n21 * n33 * n42 + n22 * n31 * n43 - n21 * n32 * n43 ) * detInv;

    te[ 4 ] = t12 * detInv;
    te[ 5 ] = ( n13 * n34 * n41 - n14 * n33 * n41 + n14 * n31 * n43 - n11 * n34 * n43 - n13 * n31 * n44 + n11 * n33 * n44 ) * detInv;
    te[ 6 ] = ( n14 * n32 * n41 - n12 * n34 * n41 - n14 * n31 * n42 + n11 * n34 * n42 + n12 * n31 * n44 - n11 * n32 * n44 ) * detInv;
    te[ 7 ] = ( n12 * n33 * n41 - n13 * n32 * n41 + n13 * n31 * n42 - n11 * n33 * n42 - n12 * n31 * n43 + n11 * n32 * n43 ) * detInv;

    te[ 8 ] = t13 * detInv;
    te[ 9 ] = ( n14 * n23 * n41 - n13 * n24 * n41 - n14 * n21 * n43 + n11 * n24 * n43 + n13 * n21 * n44 - n11 * n23 * n44 ) * detInv;
    te[ 10 ] = ( n12 * n24 * n41 - n14 * n22 * n41 + n14 * n21 * n42 - n11 * n24 * n42 - n12 * n21 * n44 + n11 * n22 * n44 ) * detInv;
    te[ 11 ] = ( n13 * n22 * n41 - n12 * n23 * n41 - n13 * n21 * n42 + n11 * n23 * n42 + n12 * n21 * n43 - n11 * n22 * n43 ) * detInv;

    te[ 12 ] = t14 * detInv;
    te[ 13 ] = ( n13 * n24 * n31 - n14 * n23 * n31 + n14 * n21 * n33 - n11 * n24 * n33 - n13 * n21 * n34 + n11 * n23 * n34 ) * detInv;
    te[ 14 ] = ( n14 * n22 * n31 - n12 * n24 * n31 - n14 * n21 * n32 + n11 * n24 * n32 + n12 * n21 * n34 - n11 * n22 * n34 ) * detInv;
    te[ 15 ] = ( n12 * n23 * n31 - n13 * n22 * n31 + n13 * n21 * n32 - n11 * n23 * n32 - n12 * n21 * n33 + n11 * n22 * n33 ) * detInv;
}

inline void matrixTranspose(const GLfloat* m, GLfloat* o)
{
    if (o == m)
    {
        GLfloat t;
        t = m[1];
        o[1] = m[4];
        o[4] = t;
        t = m[2];
        o[2] = m[8];
        o[8] = t;
        t = m[3];
        o[3] = m[12];
        o[12] = t;
        t = m[6];
        o[6] = m[9];
        o[9] = t;
        t = m[7];
        o[7] = m[13];
        o[13] = t;
        t = m[11];
        o[11] = m[14];
        o[14] = t;
    }
    else
    {
        o[1] = m[4];
        o[4] = m[1];
        o[2] = m[8];
        o[8] = m[2];
        o[3] = m[12];
        o[12] = m[3];
        o[6] = m[9];
        o[9] = m[6];
        o[7] = m[13];
        o[13] = m[7];
        o[11] = m[14];
        o[14] = m[11];
    }
    o[0] = m[0];
    o[5] = m[5];
    o[10] = m[10];
    o[15] = m[15];
}

inline double dot(const double* v1, const double* v2)
{
	return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2];
}

inline void transformPlane(const double* p, const float* m, double* pr)
{
	double pCopy[3] = { p[0], p[1], p[2] };

	pr[0] = p[0] * -p[3];
	pr[1] = p[1] * -p[3];
	pr[2] = p[2] * -p[3];

	multiplyMatrixWithVector(m, pr, pr);
	float mCopy[16];
	double normal[3];
	matrixInverse(m, mCopy);
	matrixTranspose(mCopy, mCopy);
	multiplyMatrixWithVector(mCopy, pCopy, normal, false);

	pr[3] = -dot(pr, normal);
	pr[0] = normal[0];
	pr[1] = normal[1];
	pr[2] = normal[2];

  // if (!out_plane) {
  //   LOGE("PlaneFitting: Invalid input to plane transform");
  //   return;
  // }

  // const glm::vec4 input_normal(glm::vec3(in_plane), 0.0f);
  // const glm::vec4 input_origin(
  //     -static_cast<float>(in_plane[3]) * glm::vec3(input_normal), 1.0f);

  // const glm::vec4 out_origin = out_T_in * input_origin;
  // const glm::vec4 out_normal =
  //     glm::transpose(glm::inverse(out_T_in)) * input_normal;

  // *out_plane =
  //     glm::vec4(glm::vec3(out_normal),
  //               -glm::dot(glm::vec3(out_origin), glm::vec3(out_normal)));
}

}

namespace tango_chromium {

TangoHandler* TangoHandler::instance = 0;

TangoHandler* TangoHandler::getInstance()
{
	if (instance == 0)
	{
		instance = new TangoHandler();
	}
	return instance;
}

void TangoHandler::releaseInstance()
{
	delete instance;
	instance = 0;
}

TangoHandler::TangoHandler(): connected(false)
	, tangoConfig(nullptr)
	, lastTangoImageBufferTimestamp(0)
	, latestTangoXYZij(0)
	, latestTangoXYZijRetrieved(false)
	, maxPointCloudVertexCount(0)
	, pointCloudManager(0)
	, cameraImageYUV(0)
	, cameraImageYUVSize(0)
	, cameraImageYUVTemp(0)
	, cameraImageYUVOffset(0)
	, cameraImageRGB(0)
	, cameraImageRGBSize(0)
	, cameraImageWidth(0)
	, cameraImageHeight(0)
	, cameraImageTextureWidth(0)
	, cameraImageTextureHeight(0)
	, cameraImageYUVHasChanged(false)
{
    pthread_mutexattr_t	attr;
    pthread_mutexattr_init( &attr );
    pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_ERRORCHECK );
    // pthread_mutex_init( &pointCloudMutex, &attr );
    pthread_mutex_init( &cameraImageMutex, &attr );
    pthread_cond_init( &cameraImageCondition, NULL );
    pthread_mutexattr_destroy( &attr );	
}

TangoHandler::~TangoHandler() 
{
    // pthread_mutex_destroy( &pointCloudMutex );
    pthread_mutex_destroy( &cameraImageMutex );
    pthread_cond_destroy( &cameraImageCondition );
}

void TangoHandler::onCreate(JNIEnv* env, jobject activity) 
{
	// Check the installed version of the TangoCore.  If it is too old, then
	// it will not support the most up to date features.
	int version = 0;
	TangoErrorType result;

	result = TangoSupport_GetTangoVersion(env, activity,
			&version);
	if (result != TANGO_SUCCESS || version < kTangoCoreMinimumVersion) 
	{
		LOGE("TangoHandler::OnCreate, Tango Core version is out of date.");
		std::exit (EXIT_SUCCESS);
	}
}

void TangoHandler::onTangoServiceConnected(JNIEnv* env, jobject binder) 
{
	TangoErrorType result;

	if (TangoService_setBinder(env, binder) != TANGO_SUCCESS) {
		LOGE("TangoHandler::OnTangoServiceConnected, TangoService_setBinder error");
		std::exit (EXIT_SUCCESS);
	}

	// TANGO_CONFIG_DEFAULT is enabling Motion Tracking and disabling Depth
	// Perception.
	tangoConfig = TangoService_getConfig(TANGO_CONFIG_DEFAULT);
	if (tangoConfig == nullptr) 
	{
		LOGE("TangoHandler::OnTangoServiceConnected, TangoService_getConfig error.");
		std::exit (EXIT_SUCCESS);
	}

	// Enable Depth Perception.
	result = TangoConfig_setBool(tangoConfig, "config_enable_depth", true);
	if (result != TANGO_SUCCESS) 
	{
		LOGE("TangoHandler::OnTangoServiceConnected, config_enable_depth activation failed with error code: %d.", result);
		std::exit(EXIT_SUCCESS);
	}

	// Note that it is super important for AR applications that we enable low
	// latency IMU integration so that we have pose information available as
	// quickly as possible. Without setting this flag, you will often receive
	// invalid poses when calling getPoseAtTime() for an image.
	result = TangoConfig_setBool(tangoConfig, "config_enable_low_latency_imu_integration", true);
	if (result != TANGO_SUCCESS) {
		LOGE("TangoHandler::OnTangoServiceConnected, failed to enable low latency imu integration.");
		std::exit(EXIT_SUCCESS);
	}

#ifdef TANGO_USE_DRIFT_CORRECTION
	// Drift correction allows motion tracking to recover after it loses tracking.
	// The drift corrected pose is is available through the frame pair with
	// base frame AREA_DESCRIPTION and target frame DEVICE.
	result = TangoConfig_setBool(tangoConfig, "config_enable_drift_correction", true);
	if (result != TANGO_SUCCESS) {
		LOGE("TangoHandler::OnTangoServiceConnected, enabling config_enable_drift_correction "
			"failed with error code: %d", result);
		std::exit(EXIT_SUCCESS);
	}
#endif	

#ifdef TANGO_USE_POINT_CLOUD
	int maxPointCloudVertexCount_temp = 0;
	result = TangoConfig_getInt32(tangoConfig, "max_point_cloud_elements", &maxPointCloudVertexCount_temp);
	if (result != TANGO_SUCCESS) 
	{
		LOGE("TangoHandler::OnTangoServiceConnected, Get max_point_cloud_elements failed");
		std::exit(EXIT_SUCCESS);
	}
	maxPointCloudVertexCount = static_cast<uint32_t>(maxPointCloudVertexCount_temp);

    result = TangoSupport_createPointCloudManager(maxPointCloudVertexCount, &pointCloudManager);
    if (result != TANGO_SUCCESS) 
    {
		LOGE("TangoHandler::OnTangoServiceConnected, TangoSupport_createPointCloudManager failed");
		std::exit(EXIT_SUCCESS);
    }

	// Attach the OnXYZijAvailable callback to the onPointCloudAvailable
	// function defined above. The callback will be called every time a new
	// point cloud is acquired, after the service is connected.
	result = TangoService_connectOnXYZijAvailable(::onTangoXYZijAvailable);
	if (result != TANGO_SUCCESS) 
	{
		LOGE("TangoHandler::OnTangoServiceConnected, Failed to connect to point cloud callback with error code: %d", result);
		std::exit(EXIT_SUCCESS);
	}
#endif

#ifdef TANGO_USE_CAMERA	
	// Enable color camera from config.
	result = TangoConfig_setBool(tangoConfig, "config_enable_color_camera", true);
	if (result != TANGO_SUCCESS) {
		LOGE("TangoHandler::OnTangoServiceConnected, config_enable_color_camera() failed with error code: %d", result);
		std::exit(EXIT_SUCCESS);
	}

	result = TangoService_connectOnFrameAvailable(TANGO_CAMERA_COLOR, this, ::onCameraFrameAvailable);
	if (result != TANGO_SUCCESS) {
		LOGE("TangoHandler::OnTangoServiceConnected, Error connecting color frame %d", result);
		std::exit(EXIT_SUCCESS);
	}
#endif

	if (TangoService_connect(this, tangoConfig) != TANGO_SUCCESS) 
	{
		LOGE("TangoHandler::OnTangoServiceConnected, TangoService_connect error.");
		std::exit (EXIT_SUCCESS);
	}

	// Get the intrinsics for the color camera and pass them on to the depth
	// image. We need these to know how to project the point cloud into the color
	// camera frame.
	result = TangoService_getCameraIntrinsics(TANGO_CAMERA_COLOR, &tangoCameraIntrinsics);
	if (result != TANGO_SUCCESS) {
		LOGE("PlaneFittingApplication: Failed to get the intrinsics for the color camera.");
		std::exit(EXIT_SUCCESS);
	}

	// By default, use the camera width and height retrieved from the tango camera intrinsics.
	cameraImageWidth = cameraImageTextureWidth = tangoCameraIntrinsics.width;
	cameraImageHeight = cameraImageTextureHeight = tangoCameraIntrinsics.height;

	// Initialize TangoSupport context.
	TangoSupport_initialize(TangoService_getPoseAtTime);

	connected = true;
}

void TangoHandler::onPause() 
{
	// pthread_mutex_lock( &pointCloudMutex );

	TangoSupport_freePointCloudManager(pointCloudManager);

	// pthread_mutex_unlock( &pointCloudMutex );

	TangoConfig_free(tangoConfig);
	tangoConfig = nullptr;
	TangoService_disconnect();

	pthread_mutex_lock( &cameraImageMutex );

	cameraImageYUVSize = cameraImageYUVOffset = 0;
	cameraImageYUVHasChanged = false;
	delete [] cameraImageYUVTemp;
	cameraImageYUVTemp = 0;
	delete [] cameraImageYUV;
	cameraImageYUV = 0;

	cameraImageRGBSize = cameraImageWidth = cameraImageHeight = cameraImageTextureWidth = cameraImageTextureHeight = 0;
	delete [] cameraImageRGB;
	cameraImageRGB = 0;

	pthread_mutex_unlock( &cameraImageMutex );

	connected = false;
}

bool TangoHandler::isConnected() const
{
	return connected;
}

bool TangoHandler::getPose(TangoPoseData* tangoPoseData) 
{
	bool result = connected;
	if (connected)
	{
		result = TangoSupport_getPoseAtTime(
			0.0, TANGO_COORDINATE_FRAME_START_OF_SERVICE,
			TANGO_COORDINATE_FRAME_CAMERA_COLOR, TANGO_SUPPORT_ENGINE_OPENGL,
			ROTATION_0, tangoPoseData) == TANGO_SUCCESS;
		if (!result) 
		{
			LOGE("TangoHandler::getPose: Failed to get a the pose.");
		}
	}
	return result;
}

bool TangoHandler::getPoseMatrix(float* matrix)
{
	bool result = false;
	if (!latestTangoXYZijRetrieved)
	{
		TangoSupport_getLatestPointCloud(pointCloudManager, &latestTangoXYZij);
	}
	else 
	{
		latestTangoXYZijRetrieved = false;
	}
	TangoMatrixTransformData tangoMatrixTransformData;
	TangoSupport_getMatrixTransformAtTime(
		latestTangoXYZij->timestamp, TANGO_COORDINATE_FRAME_AREA_DESCRIPTION,
		TANGO_COORDINATE_FRAME_CAMERA_COLOR, TANGO_SUPPORT_ENGINE_OPENGL,
		TANGO_SUPPORT_ENGINE_TANGO, ROTATION_0, &tangoMatrixTransformData);
	if (tangoMatrixTransformData.status_code != TANGO_POSE_VALID) {
		LOGE("TangoHandler::getPoseMatrix: Could not find a valid matrix transform at "
		"time %lf for the color camera.", latestTangoXYZij->timestamp);
		return result;
	}
	memcpy(matrix, tangoMatrixTransformData.matrix, 16 * sizeof(float));
	result = true;
	return result;
}

uint32_t TangoHandler::getMaxPointCloudVertexCount() const
{
	return maxPointCloudVertexCount;
}

bool TangoHandler::getPointCloud(uint32_t* count, float* xyz)
{
	if (connected)
	{
		TangoDoubleMatrixTransformData matrixTransform;

		// pthread_mutex_lock( &pointCloudMutex );

		TangoSupport_getLatestPointCloud(pointCloudManager, &latestTangoXYZij);
		latestTangoXYZijRetrieved = true;
		// Get depth camera transform to start of service frame in OpenGL convention
		// at the point cloud timestamp.
		TangoSupport_getDoubleMatrixTransformAtTime(
			latestTangoXYZij->timestamp, TANGO_COORDINATE_FRAME_START_OF_SERVICE,
			TANGO_COORDINATE_FRAME_CAMERA_DEPTH, TANGO_SUPPORT_ENGINE_OPENGL,
			TANGO_SUPPORT_ENGINE_TANGO, ROTATION_0, &matrixTransform);
		if (matrixTransform.status_code == TANGO_POSE_VALID) 
		{
			TangoXYZij transformedLatestTangoXYZij;
			transformedLatestTangoXYZij.xyz = new float[latestTangoXYZij->xyz_count][3];
			transformedLatestTangoXYZij.xyz_count = latestTangoXYZij->xyz_count;
			// Transform point cloud to OpenGL world
			TangoSupport_doubleTransformPointCloud(matrixTransform.matrix,
		       latestTangoXYZij, &transformedLatestTangoXYZij);
			*count = latestTangoXYZij->xyz_count;
			memcpy(xyz, transformedLatestTangoXYZij.xyz, sizeof(float) * latestTangoXYZij->xyz_count * 3);
			delete [] transformedLatestTangoXYZij.xyz;
		} 
		else 
		{
			LOGE(
				"TangoHandler::getXYZ: Could not find a valid matrix transform at "
				"time %lf for the depth camera.",
				latestTangoXYZij->timestamp);
		}

		// pthread_mutex_unlock( &pointCloudMutex );

	}
	else
	{
		*count = 0;
	}

	return connected;
}

bool TangoHandler::getPickingPointAndPlaneInPointCloud(float x, float y, double* point, double* plane)
{
	bool result = false;
	if (!latestTangoXYZijRetrieved)
	{
		TangoSupport_getLatestPointCloud(pointCloudManager, &latestTangoXYZij);
	}
	else 
	{
		latestTangoXYZijRetrieved = false;
	}

	TangoPoseData tangoPoseData;
	if (TangoSupport_calculateRelativePose(
		lastTangoImageBufferTimestamp, TANGO_COORDINATE_FRAME_CAMERA_COLOR,
		latestTangoXYZij->timestamp, TANGO_COORDINATE_FRAME_CAMERA_DEPTH,
		&tangoPoseData) != TANGO_SUCCESS)
	{
	 	LOGE("ERROR: TangoHandler::getPickingPointAndPlaneInPointCloud. Could not retrieve the pose.");
	 	return result;
	}

	// TangoPoseData tangoPoseData;
	// if (TangoSupport_getPoseAtTime(
	// 	latestTangoXYZij->timestamp, TANGO_COORDINATE_FRAME_START_OF_SERVICE,
	// 	TANGO_COORDINATE_FRAME_CAMERA_COLOR, TANGO_SUPPORT_ENGINE_OPENGL,
	// 	ROTATION_0, &tangoPoseData) != TANGO_SUCCESS)
	// {
	// 	LOGE("ERROR: TangoHandler::getPickingPointAndPlaneInPointCloud. Could not retrieve the pose.");
	// 	return result;
	// }

	float uv[] = {x, y};
	if (TangoSupport_fitPlaneModelNearClick(
		latestTangoXYZij, &tangoCameraIntrinsics,
		&tangoPoseData, uv,	point, plane) != TANGO_SUCCESS)
	{
		LOGE("ERROR: TangoHandler::getPickingPointAndPlaneInPointCloud. Could not calculate the picking point and plane.");
		return result;
	}

	TangoMatrixTransformData tangoMatrixTransformData;
	TangoSupport_getMatrixTransformAtTime(
		latestTangoXYZij->timestamp, TANGO_COORDINATE_FRAME_START_OF_SERVICE,
		TANGO_COORDINATE_FRAME_CAMERA_COLOR, TANGO_SUPPORT_ENGINE_OPENGL,
		TANGO_SUPPORT_ENGINE_TANGO, ROTATION_0, &tangoMatrixTransformData);
	if (tangoMatrixTransformData.status_code != TANGO_POSE_VALID) {
		LOGE("TangoHandler::getPickingPointAndPlaneInPointCloud: Could not find a valid matrix transform at "
		"time %lf for the color camera.", latestTangoXYZij->timestamp);
		return result;
	}

	multiplyMatrixWithVector(tangoMatrixTransformData.matrix, point, point);

//	LOGI("Before: %f, %f, %f, %f", plane[0], plane[1], plane[2], plane[3]);
	transformPlane(plane, tangoMatrixTransformData.matrix, plane);
//	LOGI("After: %f, %f, %f, %f", plane[0], plane[1], plane[2], plane[3]);

	result = true;

	return result;
}

bool TangoHandler::getCameraImageSize(uint32_t* width, uint32_t* height)
{
	bool result = true;

#ifdef TANGO_USE_YUV_CAMERA	

	pthread_mutex_lock( &cameraImageMutex );

	result = cameraImageYUVTemp != 0;

	if (!result)
	{
        pthread_cond_wait( &cameraImageCondition, &cameraImageMutex );		
	}

	*width = cameraImageWidth;
	*height = cameraImageHeight;

	pthread_mutex_unlock( &cameraImageMutex );

#else

	*width = cameraImageWidth;
	*height = cameraImageHeight;		

#endif

	return result;
}

bool TangoHandler::getCameraImageTextureSize(uint32_t* width, uint32_t* height)
{
	bool result = true;

#ifdef TANGO_USE_YUV_CAMERA	

	pthread_mutex_lock( &cameraImageMutex );

	result = cameraImageYUVTemp != 0;

	if (!result)
	{
        pthread_cond_wait( &cameraImageCondition, &cameraImageMutex );		
	}

	*width = cameraImageTextureWidth;
	*height = cameraImageTextureHeight;

	pthread_mutex_unlock( &cameraImageMutex );

#else

	*width = cameraImageTextureWidth;
	*height = cameraImageTextureHeight;

#endif	

	return result;
}

bool TangoHandler::getCameraFocalLength(double* focalLengthX, double* focalLengthY)
{
	bool result = true;
	*focalLengthX = tangoCameraIntrinsics.fx;
	*focalLengthY = tangoCameraIntrinsics.fy;
	return result;
}

bool TangoHandler::getCameraPoint(double* x, double* y)
{
	bool result = true;
	*x = tangoCameraIntrinsics.cx;
	*y = tangoCameraIntrinsics.cy;
	return result;
}

bool TangoHandler::getCameraImageRGB(uint8_t* image)
{
	bool result = false;

#ifndef TANGO_USE_YUV_CAMERA
	return result;	
#endif

	pthread_mutex_lock( &cameraImageMutex );

	result = cameraImageYUVTemp != 0;

	if (!result)
	{
        pthread_cond_wait( &cameraImageCondition, &cameraImageMutex );		
	}

	pthread_mutex_unlock( &cameraImageMutex );

	if (cameraImageYUVHasChanged)
	{
		pthread_mutex_lock( &cameraImageMutex );

		memcpy(cameraImageYUV, cameraImageYUVTemp, cameraImageYUVSize);

		pthread_mutex_unlock( &cameraImageMutex );

		for (size_t i = 0; i < cameraImageHeight; ++i) {
			for (size_t j = 0; j < cameraImageWidth; ++j) {
				size_t x_index = j;
				if (j % 2 != 0) {
					x_index = j - 1;
				}

				size_t rgb_index = (i * cameraImageTextureWidth + j) * 3;

				// The YUV texture format is NV21,
				// yuv_buffer_ buffer layout:
				//   [y0, y1, y2, ..., yn, v0, u0, v1, u1, ..., v(n/4), u(n/4)]
				yuv2Rgb(
				cameraImageYUV[i * cameraImageWidth + j],
				cameraImageYUV[cameraImageYUVOffset + (i / 2) * cameraImageWidth + x_index + 1],
				cameraImageYUV[cameraImageYUVOffset + (i / 2) * cameraImageWidth + x_index],
				&cameraImageRGB[rgb_index], &cameraImageRGB[rgb_index + 1],
				&cameraImageRGB[rgb_index + 2]);
			}
		}

	/*
		size_t xIndex = 0;
		size_t rgbIndex = 0;
		size_t yuvOffset1 = 0; 
		size_t yuvOffset2 = 0; 
		size_t rgbOffset = 0;
		for (size_t i = 0; i < cameraImageHeight; ++i) 
		{
			yuvOffset1 = i * cameraImageWidth;
			yuvOffset2 = cameraImageYUVOffset + (i / 2) * cameraImageWidth;
			rgbOffset = i * cameraImageTextureWidth;
			for (size_t j = 0; j < cameraImageWidth; ++j) {
				xIndex = j;
				if (j % 2 != 0) 
				{
					xIndex = j - 1;
				}

				rgbIndex = (rgbOffset + j) * 3;

				// The YUV texture format is NV21,
				// yuv_buffer_ buffer layout:
				//   [y0, y1, y2, ..., yn, v0, u0, v1, u1, ..., v(n/4), u(n/4)]
				yuv2Rgb(
					cameraImageYUV[yuvOffset1 + j],
					cameraImageYUV[yuvOffset2 + xIndex + 1],
					cameraImageYUV[yuvOffset2 + xIndex],
					&cameraImageRGBTemp[rgbIndex], 
					&cameraImageRGBTemp[rgbIndex + 1],
					&cameraImageRGBTemp[rgbIndex + 2]);
			}
		}
	*/
	}

	memcpy(image, cameraImageRGB, cameraImageRGBSize);

	return result;
}

bool TangoHandler::updateCameraImageIntoTexture(uint32_t textureId)
{
	LOGI("TangoHandler::updateCameraImageIntoTexture begin: textureId = %d", textureId);

	TangoErrorType result = TangoService_updateTextureExternalOes(TANGO_CAMERA_COLOR, textureId, &lastTangoImageBufferTimestamp);

	LOGI("TangoHandler::updateCameraImageIntoTexture end: textureId = %d, correct = %s", textureId, (result == TANGO_SUCCESS ? "YES" : "NO"));

	return result == TANGO_SUCCESS;
}

void TangoHandler::onTangoXYZijAvailable(const TangoXYZij* tangoXYZij)
{
	// pthread_mutex_lock( &pointCloudMutex );

	TangoSupport_updatePointCloud(pointCloudManager, tangoXYZij);

	// pthread_mutex_unlock( &pointCloudMutex );
}

void TangoHandler::onCameraFrameAvailable(const TangoImageBuffer* buffer) 
{
#ifndef TANGO_USE_YUV_CAMERA
	return;
#endif

	if (buffer->format != TANGO_HAL_PIXEL_FORMAT_YCrCb_420_SP) 
	{
		LOGE("TangoHandler::onCameraFrameAvailable texture format is not supported by this app");
		return;
	}

	if (cameraImageYUVTemp == 0)
	{
#ifdef TANGO_USE_POWER_OF_TWO		
		cameraImageTextureWidth = closestPowerOfTwo(buffer->width);
		cameraImageTextureHeight = closestPowerOfTwo(buffer->height);
#else
		cameraImageTextureWidth = buffer->width;
		cameraImageTextureHeight = buffer->height;
#endif			
		cameraImageWidth = buffer->width;
		cameraImageHeight = buffer->height;

		pthread_mutex_lock( &cameraImageMutex );

		cameraImageYUVOffset =  buffer->width * buffer->height;
		cameraImageYUVSize = cameraImageWidth * cameraImageHeight + cameraImageWidth * cameraImageHeight / 2;
		cameraImageYUV = new uint8_t[cameraImageYUVSize];
		cameraImageYUVTemp = new uint8_t[cameraImageYUVSize];

		cameraImageRGBSize = cameraImageTextureWidth * cameraImageTextureHeight * 3;
		cameraImageRGB = new uint8_t[cameraImageRGBSize];

        pthread_cond_broadcast( &cameraImageCondition );

		pthread_mutex_unlock( &cameraImageMutex );
	}

	pthread_mutex_lock( &cameraImageMutex );

	memcpy(cameraImageYUVTemp, buffer->data, cameraImageYUVSize);

	cameraImageYUVHasChanged = true;
	lastTangoImageBufferTimestamp = buffer->timestamp;

	pthread_mutex_unlock( &cameraImageMutex );
}

}  // namespace hello_motion_tracking