/**
 * MIT License
 * 
 * Copyright (c) 2020 IWT Wirtschaft und Technik GmbH
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 * @file box_detector.cpp
 * @author Vishnuprasad Prachandabhanu (vishnu.pbhat93@gmail.com)
 */

#include <ros/ros.h>
#include <visualization_msgs/Marker.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <tf2/LinearMath/Matrix3x3.h>
#include <pcl/common/eigen.h>
#include <box_detector/box_detector.hpp>

template class nimbus::BoxDetector<pcl::PointXYZ>;

template <class PointType>
nimbus::BoxDetector<PointType>::BoxDetector(ros::NodeHandle nh): _nh(nh){
    _pub_marker = _nh.advertise<visualization_msgs::Marker> ("bounding_box", 1);
    _marker.header.frame_id = "camera";
    _marker.ns = "basic_shapes";
    _marker.id = 0;
    _marker.type = _shape;
    _marker.action = visualization_msgs::Marker::ADD;
    _marker.color.r = 0.0f;
    _marker.color.g = 1.0f;
    _marker.color.b = 0.0f;
    _marker.color.a = 1.0;

    cornerBuffer.setZero();
    cornerBufferCounter = 0;
}
template <class PointType>
nimbus::BoxDetector<PointType>::~BoxDetector(){}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <class PointType>
void 
nimbus::BoxDetector<PointType>::zAxisLimiter(const boost::shared_ptr<const pcl::PointCloud<pcl::PointXYZ>> &blob,
                                             double max, double min,
                                             pcl::PointCloud<pcl::PointXYZ> &res)
{
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>());
    pcl::PointCloud<pcl::PointXYZ>::Ptr result (new pcl::PointCloud<pcl::PointXYZ>());
    pcl::copyPointCloud(*blob, *cloud); 
    // To Carry other info
    pcl::copyPointCloud(*blob, *result);

    // Check the size of input points
    if(cloud->points.empty())
    {
        ROS_ERROR("Input Point Cloud is empty");
        return;
    }

    for (size_t i = 0; i < cloud->points.size(); ++i)
    {
        if((max >= cloud->points[i].z) && (min <= cloud->points[i].z))
        {
            pcl::copyPoint(cloud->points[i], result->points[i]);
        }else{
            result->points[i].x = NAN;
            result->points[i].y = NAN;
            result->points[i].y = NAN;
        }
    }

    // Copy to result
    pcl::copyPointCloud(*result, res);
}

template <class PointType>
void nimbus::BoxDetector<PointType>::outlineRemover(const boost::shared_ptr< const pcl::PointCloud<PointType>> blob, 
                    int width, int height, float perW, float perH,
                    pcl::PointCloud<PointType> &res)
{
    int hLower = (height*perH)/2;
    int hUpper = (height - hLower);
    int wLower = (width*perW)/2;
    int wUpper = width - wLower;
    int pCounter = 0;
    //res.width = width-wLower-wUpper;
    //res.height = height-hLower-hUpper;
    for(int i = 0; i < height; i++){
        std::vector<float> tempX;
        std::vector<float> tempY;
        std::vector<float> tempZ;
        for(int j = 0; j < width; j++){
            if((i > hLower & i < hUpper) & (j > wLower & j < wUpper)){
                tempX.push_back(blob->points[pCounter].x);
                tempY.push_back(blob->points[pCounter].y);
                tempZ.push_back(blob->points[pCounter].z);
                pCounter ++;
            }else pCounter ++;
        }
        if((i > hLower & i < hUpper)){
            for(int i = 0; i < tempX.size(); i++){
                PointType temP;
                temP.x = tempX[i];
                temP.y = tempY[i];
                temP.z = tempZ[i];
                res.points.push_back(temP);
            }
        }
    }
    res.header   = blob->header;
    res.is_dense = blob->is_dense;
    res.sensor_orientation_ = blob->sensor_orientation_;
    res.sensor_origin_ = blob->sensor_origin_;
    res.width = res.points.size();
    res.height = 1;
}

template <class PointType>
bool
nimbus::BoxDetector<PointType>::getBaseModel(const boost::shared_ptr<const pcl::PointCloud<pcl::PointXYZ>> &groud,
                                 const boost::shared_ptr<const pcl::PointCloud<pcl::PointXYZ>> &raw,
                                 double tolerence,
                                 const boost::filesystem::path &path,
                                 pcl::PointCloud<pcl::PointXYZ> &res)
{
    if(groud->points.size() != raw->points.size()){
        ROS_ERROR ("Size of ground truth and raw point cloud is not matching");
        ROS_WARN ("Deleting the current file");
        boost::filesystem::remove(path);
        return false;
    }
    typename pcl::PointCloud<pcl::PointXYZ>::Ptr edit_cloud (new pcl::PointCloud<pcl::PointXYZ>());
    // Store the difference of z axis values;
    std::vector<float> absZ;
    for (int i = 0; i < groud->points.size(); i ++){
        if(!std::isnan(groud->points[i].z) && !std::isnan(raw->points[i].z))
            absZ.push_back(std::abs(groud->points[i].z - raw->points[i].z));
        else
            absZ.push_back(NAN);
    }
    
    for (int i = 0; i < absZ.size(); i ++){
        pcl::PointXYZ temp;
        if(!std::isnan(absZ[i]) && absZ[i] > tolerence)
        {
            temp.x = raw->points[i].x;
            temp.y = raw->points[i].y;
            temp.z = raw->points[i].z;
        }
        else{
            temp.x = NAN;
            temp.y = NAN;
            temp.z = NAN;
        }
        edit_cloud->points.push_back(temp);
    }
    pcl::copyPointCloud(*edit_cloud, res);
    res.header   = raw->header;
    res.is_dense = raw->is_dense;
    res.sensor_orientation_ = raw->sensor_orientation_;
    res.sensor_origin_ = raw->sensor_origin_;
    res.height = raw->height;
    res.width = raw->width;
    return true;
}
        
template <class PointType>
bool 
nimbus::BoxDetector<PointType>::computePointNormal(const boost::shared_ptr<const pcl::PointCloud<pcl::PointXYZ>> &blob,
                                                   Eigen::Vector4f &plane_parameters, float &curvature)
{
    if(blob->points.size() < 3 ||
       boxMeanAndCovarianceMatrix(blob, _covariance_matrix, _centroid) == 0)
    {
        plane_parameters.setConstant(std::numeric_limits<float>::quiet_NaN());
        curvature = std::numeric_limits<float>::quiet_NaN();
        return false;
    }
    // Get the plane normal and surface curvature
    solveBoxParameters(_covariance_matrix, _centroid, plane_parameters, curvature);
    return true;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <class PointType>
void 
nimbus::BoxDetector<PointType>::box3DCentroid(const boost::shared_ptr<const pcl::PointCloud<pcl::PointXYZ>> &blob,
                                              Eigen::Matrix<float, 4, 1> &centroid)
{
    pcl::PointCloud<pcl::PointXYZ> cloud;
    pcl::copyPointCloud(*blob, cloud);
    if(cloud.points.empty())
    {
        ROS_ERROR("Input Point Cloud is empty");
        return;
    }
    if(cloud.is_dense)
    {
        ROS_ERROR_NAMED("Covariance Matrix", "!Dense Cloud is Not Acceptable!");
        return;
    }

    // Initialize centroid to zero
    centroid.setZero();
    // Cloud Dense is false and contain NAN points
    unsigned cp = 0;
    for(const auto& point: cloud)
    {
        if(!pcl::isFinite(point))
            continue;
        
        centroid[0] += point.x;
        centroid[1] += point.y;
        centroid[2] += point.z;
        ++cp;
    }
    centroid /= static_cast<float>(cp);
    centroid[3] = 1;
    ROS_DEBUG_NAMED("Computed 3D Centroid","Calculated Points: %f", static_cast<float>(cp));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <class PointType>
inline unsigned int 
nimbus::BoxDetector<PointType>::boxCovarianceMatrix (const boost::shared_ptr<const pcl::PointCloud<pcl::PointXYZ>> &blob,
                                          const Eigen::Matrix<float, 4, 1> &centroid,
                                          Eigen::Matrix<float, 3, 3> &covariance_matrix)
{
    pcl::PointCloud<pcl::PointXYZ> cloud;
    pcl::copyPointCloud(*blob, cloud);
    if(cloud.points.empty())
    {
        ROS_ERROR("Input Point Cloud is empty");
        return 0;
    }
    if(cloud.is_dense)
    {
        ROS_ERROR_NAMED("Covariance Matrix", "!Dense Cloud is Not Acceptable!");
        return 0;
    }

    // Initialize centroid to zero
    covariance_matrix.setZero();

    // Cloud Dense is false and contain NAN points
    unsigned cp = 0;
    for (const auto& point: cloud)
    {
        if(!pcl::isFinite(point))
            continue;
        
        Eigen::Matrix<float, 4, 1> pt;
        pt[0] = point.x - centroid[0];
        pt[1] = point.y - centroid[1];
        pt[2] = point.z - centroid[2];

        covariance_matrix (1, 1) += pt.y() * pt.y();
        covariance_matrix (1, 2) += pt.y() * pt.z();
        covariance_matrix (2, 2) += pt.z() * pt.z();
        pt *= pt.x();
        covariance_matrix (0, 0) += pt.x();
        covariance_matrix (0, 1) += pt.y();
        covariance_matrix (0, 2) += pt.z();
        ++ cp;
    }
    covariance_matrix (1, 0) = covariance_matrix (0, 1);
    covariance_matrix (2, 0) = covariance_matrix (0, 2);
    covariance_matrix (2, 1) = covariance_matrix (1, 2);

    ROS_DEBUG_NAMED("Computed Covariance Matrix","Calculated Points: %f", static_cast<float>(cp));
    return (cp);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <class PointType>
void 
nimbus::BoxDetector<PointType>::boxCovarianceMatrixNormalized(const boost::shared_ptr<const pcl::PointCloud<pcl::PointXYZ>> &blob,
                                               const Eigen::Matrix<float, 4, 1> &centroid,
                                               Eigen::Matrix<float, 3, 3> &covariance_matrix)
{
    unsigned point_count = this->boxCovarianceMatrix(blob, centroid, covariance_matrix);
    if(point_count != 0)
        covariance_matrix /= static_cast<float>(point_count);
    
    ROS_DEBUG_NAMED("Computed Covariance Matrix Normalized","Calculated Points: %f", static_cast<float>(point_count));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <class PointType>
inline unsigned int 
nimbus::BoxDetector<PointType>::boxMeanAndCovarianceMatrix (const boost::shared_ptr<const pcl::PointCloud<pcl::PointXYZ>> &blob,
                                                            Eigen::Matrix<float, 3, 3> &covariance_matrix,
                                                            Eigen::Matrix<float, 4, 1> &centroid)
{
    pcl::PointCloud<pcl::PointXYZ> cloud;
    pcl::copyPointCloud(*blob, cloud);
    if(cloud.points.empty())
    {
        ROS_ERROR("Input Point Cloud is empty");
        return 0;
    }
    if(cloud.is_dense)
    {
        ROS_ERROR_NAMED("Covariance Matrix", "!Dense Cloud is Not Acceptable!");
        return 0;
    }

    Eigen::Matrix<float, 1, 9, Eigen::RowMajor> accu = Eigen::Matrix<float, 1, 9, Eigen::RowMajor>::Zero();
    std::size_t point_count = 0;
    
    for(const auto& point: cloud)
    {
        if(!pcl::isFinite(point))
            continue;
        
        accu[0] += point.x * point.x;
        accu[1] += point.x * point.y;
        accu[2] += point.x * point.z;
        accu[3] += point.y * point.y;
        accu[4] += point.y * point.z;
        accu[5] += point.z * point.z;
        accu[6] += point.x;
        accu[7] += point.y;
        accu[8] += point.z;
        ++ point_count;
    } 

    accu /= static_cast<float>(point_count);
    if(point_count != 0)
    {
        centroid[0] = accu[6];
        centroid[1] = accu[7];
        centroid[2] = accu[8];
        centroid[3] = 1;

        covariance_matrix.coeffRef(0) = accu[0] - accu[6] * accu[6];
        covariance_matrix.coeffRef(1) = accu[1] - accu[6] * accu[7];
        covariance_matrix.coeffRef(2) = accu[2] - accu[6] * accu[8];
        covariance_matrix.coeffRef(4) = accu[3] - accu[7] * accu[7];
        covariance_matrix.coeffRef(5) = accu[4] - accu[7] * accu[8];
        covariance_matrix.coeffRef(8) = accu[5] - accu[8] * accu[8];
        covariance_matrix.coeffRef(3) = covariance_matrix.coeff(1);
        covariance_matrix.coeffRef(6) = covariance_matrix.coeff(2);
        covariance_matrix.coeffRef(7) = covariance_matrix.coeff(5);
    }
    ROS_DEBUG_NAMED("Computed Mean Covariance Matrix","Calculated Points: %f", static_cast<float>(point_count));
    return (point_count);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <class PointType>
void
nimbus::BoxDetector<PointType>::solveBoxParameters (const Eigen::Matrix3f &covariance_matrix,
                                                    const Eigen::Vector4f &centroid,
                                                    Eigen::Vector4f &plane_param, float &curvature)
{
    solveBoxParameters(covariance_matrix, plane_param[0], plane_param[1], plane_param[2], curvature);

    plane_param[3] = 0;
    // Hessian form (D = nc . p_plane (centroid here) + p)
    plane_param[3] = -1 * plane_param.dot(centroid);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <class PointType>
void 
nimbus::BoxDetector<PointType>::solveBoxParameters (const Eigen::Matrix3f &covariance_matrix,
                                       float &nx, float &ny, float &nz, float &curvature)
{
    EIGEN_ALIGN16 Eigen::Vector3f::Scalar eigen_value;
    EIGEN_ALIGN16 Eigen::Vector3f eigen_vector;
    pcl::eigen33(covariance_matrix, eigen_value, eigen_vector);

    nx = eigen_vector[0];
    ny = eigen_vector[1];
    nz = eigen_vector[2];

    // Compute the surface curvature
    float eigen_sum = covariance_matrix.coeff(0) + covariance_matrix.coeff(4) + covariance_matrix.coeff(8);
    if(eigen_sum != 0)
        curvature = std::abs(eigen_value / eigen_sum);
    else
        curvature = 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <class PointType>
bool 
nimbus::BoxDetector<PointType>::boxYaw(const boost::shared_ptr<const pcl::PointCloud<pcl::PointXYZ>> &blob,
                                       const float width, const float length,
                                       const Eigen::Vector4f &centroid,
                                       float &yaw)
{
    Eigen::Matrix<float, 4, 2> corners;
    corners.setZero();
    bool ready = this->getMeanCorners(blob, 5);
    if(!ready) return false;
    corners = cornerBuffer;
    cornerBuffer.setZero();
    // Calculate the best corner 
    // Diagonal
    float d = sqrt((width * width) + (length * length));

    unsigned int best_corner = 0;
    this->selectBestCorner(d, corners, centroid, best_corner);

    float m = 0;
    float angle = 0;
    float box_max_angle = atan(length/width);
    float box_min_angle = atan(width/length);
    switch(best_corner)
    {
        case 0:
            m = static_cast<float>(slope(corners(0, 0), corners(0, 1), centroid[0], centroid[1]));
            angle = atan(m);
            ROS_ERROR("Actual slope: %f", (angle * 180)/M_PI);
            // Look Ymin side is Length or width
            this->selectSide(corners(0, 0), corners(0, 1), corners(2,0), corners(2,1), width, length, sideSelect);
            if (sideSelect == Side::LENGTH){
                if(((angle * 180)/M_PI) < 0){
                    angle = M_PI + angle;
                    yaw = -(M_PI/2) + angle - box_min_angle;
                } else{
                    yaw =  angle + box_max_angle;
                }
                ROS_ERROR ("Xmin - Length Side Slope angle: %f, yaw: %f", (angle * 180)/M_PI, (yaw * 180)/M_PI);
            }
            if(sideSelect == Side::WIDTH){
                if(((angle * 180)/M_PI) < 0){
                    // Test Complete
                    angle = M_PI + angle;
                    yaw = - angle + box_min_angle;
                } else{
                    yaw = angle - box_max_angle;
                }
                ROS_ERROR ("Xmin - Width Side Slope angle: %f, yaw: %f", (angle * 180)/M_PI, (yaw * 180)/M_PI);
            }
            break;
        case 1:
            m = static_cast<float>(slope(corners(1, 0), corners(1, 1), centroid[0], centroid[1]));
            angle = atan(m);
            ROS_ERROR("Actual slope: %f", (angle * 180)/M_PI);
            // Look Ymin side is Length or width
            this->selectSide(corners(1,0), corners(1,1), corners(2,0), corners(2,1), width, length, sideSelect);
            if (sideSelect == Side::LENGTH){
                if(((angle * 180)/M_PI) < 0){
                    yaw =  angle + box_max_angle;
                }else{
                    yaw = angle - box_max_angle;
                }
                ROS_ERROR ("Xmax - Length Side Slope angle: %f, yaw: %f", (angle * 180)/M_PI, (yaw * 180)/M_PI);
            }
            if(sideSelect == Side::WIDTH){
                if(((angle * 180)/M_PI) < 0){
                    yaw = (M_PI/2) + angle - box_min_angle;
                }else{
                    yaw =  angle + (M_PI/2) - box_min_angle;
                }
                ROS_ERROR ("Xmax - Width Side Slope angle: %f, yaw: %f", (angle * 180)/M_PI, (yaw * 180)/M_PI);
            }
            break;
        case 2:
            m = static_cast<float>(slope(corners(2, 0), corners(2, 1), centroid[0], centroid[1]));
            angle = atan(m);
            ROS_ERROR("Actual slope: %f", (angle * 180)/M_PI);
            // Look Xmin side is Length or width
            this->selectSide(corners(2,0), corners(2,1), corners(0,0), corners(0,1), width, length, sideSelect);
            if (sideSelect == Side::LENGTH){
                if((angle * 180)/M_PI < 0) yaw = angle - box_min_angle; // Check the sign of angle
                else yaw = angle - box_max_angle; // Todo
                ROS_ERROR ("Ymin - Length Side Slope angle: %f, yaw: %f", (angle * 180)/M_PI, (yaw * 180)/M_PI);
            }
            if(sideSelect == Side::WIDTH){
                if((angle * 180)/M_PI < 0) yaw = angle - box_max_angle; // ToDo
                else yaw = angle - box_max_angle;
                ROS_ERROR ("Ymin - Width Side Slope angle: %f, yaw: %f", (angle * 180)/M_PI, (yaw * 180)/M_PI);
            }
            break;
        case 3:
            m = static_cast<float>(slope(corners(3,0), corners(3, 1), centroid[0], centroid[1]));
            angle = atan(m);
            ROS_ERROR("Actual slope: %f", (angle * 180)/M_PI);
            // Look Xmin side is Length or width
            this->selectSide(corners(3,0), corners(3,1), corners(0,0), corners(0,1), width, length, sideSelect);
            if (sideSelect == Side::LENGTH){
                if((angle * 180)/M_PI < 0) yaw =  angle + box_min_angle + (M_PI/2);
                else yaw = angle - box_min_angle + (M_PI/2);
                ROS_ERROR ("Ymax - Length Side Slope angle: %f, yaw: %f", (angle * 180)/M_PI, (yaw * 180)/M_PI);
            }
            if(sideSelect == Side::WIDTH){
                if((angle * 180)/M_PI < 0) yaw =  box_max_angle + angle; // Check the sign of angle
                else yaw = angle - box_max_angle; 
                ROS_ERROR ("Ymax - Width Side Slope angle: %f, yaw: %f", (angle * 180)/M_PI, (yaw * 180)/M_PI);
            }
            break;
    }

    _marker.lifetime = ros::Duration(); 
    _marker.pose.position.x = centroid[0];
    _marker.pose.position.y = centroid[1];
    _marker.pose.position.z = centroid[2];
    tf2::Quaternion q;
    q.setRPY(0, 0, yaw);
    _marker.pose.orientation = tf2::toMsg(q);
    _marker.scale.x = width;
    _marker.scale.y = length;
    _marker.scale.z = 0.001;
    _pub_marker.publish(_marker);
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <class PointType>
void 
nimbus::BoxDetector<PointType>::selectSide(const float x1, const float y1, const float x2, const float y2, 
                                           const float width, const float length, Side &sides)
{
    float wl = static_cast<float>(hypotenuse((x2 - x1), (y2 - y1)));

    float lCorrection = std::abs(length - wl);
    float wCorrection = std::abs(width - wl);

    if(lCorrection < wCorrection) sides = Side::LENGTH;
    if(wCorrection < lCorrection) sides = Side::WIDTH;

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <class PointType>
bool 
nimbus::BoxDetector<PointType>::getMeanCorners(const boost::shared_ptr<const pcl::PointCloud<pcl::PointXYZ>> &blob, int frameSize)
{
    Eigen::Matrix<float, 4, 2> corners;
    pcl::PointCloud<pcl::PointXYZ> cloud;
    pcl::copyPointCloud(*blob, cloud);

    size_t i = 0;
    corners(0, 0) = corners(1, 0) = cloud.points[i].x;
    corners(2, 1) = corners(3, 1) = cloud.points[i].y;
    i += 1;
    while (std::isnan(cloud.points[i].x))
    {
        corners(0, 0) = corners(1, 0) = cloud.points[i].x;
        corners(2, 1) = corners(3, 1) = cloud.points[i].y;
        i += 1;
    }
    // Calculate Maximum and Minimum values in X and Y axis
    corners.setZero();
    for(size_t j = i; j < cloud.points.size(); ++j)
    {
        pcl::PointXYZ point = cloud.points[j];
        if(!pcl::isFinite(point)) continue;

        if(cloud.points[j].x < corners(0, 0))
        {
            // Xmin
            corners(0, 0) = cloud.points[j].x;
            corners(0, 1) = cloud.points[j].y;
        }
        if(cloud.points[j].x > corners(1, 0))
        {
            // Xmax
            corners(1, 0) = cloud.points[j].x;
            corners(1, 1) =cloud.points[j].y;
        }
        if(cloud.points[j].y < corners(2, 1))
        {
            // Ymin
            corners(2, 1) = cloud.points[j].y;
            corners(2, 0) = cloud.points[j].x;
        }
        if(cloud.points[j].y > corners(3, 1))
        {
            // Ymax
            corners(3, 1) = cloud.points[j].y;
            corners(3, 0) = cloud.points[j].x;
        }
    }
    if(cornerBufferCounter <= frameSize){
        cornerBufferCounter += 1;
        cornerBuffer(0, 0) += corners(0, 0);
        cornerBuffer(0, 1) += corners(0, 1);
        cornerBuffer(1, 0) += corners(1, 0);
        cornerBuffer(1, 1) += corners(1, 1);
        cornerBuffer(2, 0) += corners(2, 0);
        cornerBuffer(2, 1) += corners(2, 1);
        cornerBuffer(3, 0) += corners(3, 0);
        cornerBuffer(3, 1) += corners(3, 1);
        return false;
    }else{
        cornerBuffer /= static_cast<float>(frameSize);
        cornerBufferCounter = 0;
        return true;
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <class PointType>
void 
nimbus::BoxDetector<PointType>::selectBestCorner(const float diagonal, const Eigen::Matrix<float, 4, 2> corners, 
                                                 const Eigen::Vector4f &centroid, unsigned int &best)
{
    Eigen::Vector4f halfD;
    halfD.setZero();

    halfD[0] = static_cast<float>(hypotenuse((centroid[0] - corners(0, 0)), (centroid[1] - corners(0, 1))));
    halfD[1] = static_cast<float>(hypotenuse((centroid[0] - corners(1, 0)), (centroid[1] - corners(1, 1))));
    halfD[2] = static_cast<float>(hypotenuse((centroid[0] - corners(2, 0)), (centroid[1] - corners(2, 1))));
    halfD[3] = static_cast<float>(hypotenuse((centroid[0] - corners(3, 0)), (centroid[1] - corners(3, 1))));

    float minValue = std::abs((diagonal/2) - halfD[0]);
    int indice = 0;
    for(int i = 1; i < halfD.size(); ++i)
    {
        float value = std::abs((diagonal/2) - halfD[i]);
        if(minValue > value)
        {
            minValue = value;
            indice = i;
        }
    }

    best = indice;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <class PointType>
void 
nimbus::BoxDetector<PointType>::meanFilter(pcl::SynchronizedQueue<pcl::PointCloud<pcl::PointXYZ>> &queue, 
                                            pcl::PointCloud<pcl::PointXYZ> &res)
{
    if (queue.isEmpty()) return;
    typename pcl::PointCloud<PointType>::Ptr tempC (new pcl::PointCloud<PointType>());
    queue.dequeue(*tempC);

    int width = tempC->width;
    int height = tempC->height;

    std::vector<float> conf(width*height, 0);
    std::vector<float> mnCounter(width*height, 0);
    std::vector<float> addX(width*height, 0);
    std::vector<float> addY(width*height, 0);
    std::vector<float> addZ(width*height, 0);
    
    res.width = width;
    res.height = height;
    res.header   = tempC->header;
    res.is_dense = tempC->is_dense;
    res.sensor_orientation_ = tempC->sensor_orientation_;
    res.sensor_origin_ = tempC->sensor_origin_;

    while (!queue.isEmpty()){
        int i = 0;
        for(int i = 0; i < tempC->points.size(); i++){
            if(!std::isnan(tempC->points[i].z)){
                addX[i] += tempC->points[i].x;
                addY[i] += tempC->points[i].y;
                addZ[i] += tempC->points[i].z;
                mnCounter[i] +=1;
            }else{
                conf[i] = 1;
                mnCounter[i] = 0;
            }
        }
        queue.dequeue(*tempC);
    }
    for(int i = 0; i < mnCounter.size(); i++){
        PointType temPoint;
        if(mnCounter[i] == 0){
            temPoint.x = NAN;
            temPoint.y = NAN;
            temPoint.z = NAN;
        }else{
            temPoint.x = addX[i] / mnCounter[i];
            temPoint.y = addY[i] / mnCounter[i];
            temPoint.z = addZ[i] / mnCounter[i];
        }
        res.points.push_back(temPoint);
    }
    res.is_dense = false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////