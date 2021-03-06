
#include <nimbus_fh_detector/recognition.hpp>

#include <pcl/io/pcd_io.h>
#include <pcl/common/transforms.h>
#include <pcl/recognition/cg/hough_3d.h>
#include <pcl/recognition/hv/hv_go.h>
#include <pcl/recognition/impl/hv/hv_go.hpp>
#include <pcl/registration/icp.h>
#include <pcl/visualization/pcl_visualizer.h>

nimbus::Recognition::Recognition(ros::NodeHandle nh, const std::string path): _path(path), _features(nh, 0.01, 0.01, 0.01), _nh(nh)
{
    pubPose = _nh.advertise<geometry_msgs::TransformStamped>("/iiwa/detected_pose", 5);
    
}
nimbus::Recognition::~Recognition(){}

void nimbus::Recognition::constructModelParam()
{
    _model.resize(10);
    _model_normals.resize(10);
    _model_keypoints.resize(10);
    _model_description.resize(10);
    _model_board.resize(10);
    // ToDo: Change the fixed number of *.pcd file to for loop
    
    for(int i = 0; i < 10; i++)
    {
        _model[i].reset(new pcl::PointCloud<pcl::PointXYZI>());
        _model_normals[i].reset(new pcl::PointCloud<pcl::Normal>());
        _model_keypoints[i].reset(new pcl::PointCloud<pcl::PointXYZI>());
        _model_description[i].reset(new pcl::PointCloud<pcl::SHOT352>());
        _model_board[i].reset(new pcl::PointCloud<pcl::ReferenceFrame>());

        typename pcl::PointCloud<pcl::PointXYZI>::Ptr blob (new pcl::PointCloud<pcl::PointXYZI>());
        std::stringstream model_path;
        model_path << _path << "/box_";
        model_path << std::to_string(i+1) << ".pcd";
        pcl::io::loadPCDFile(model_path.str(), *blob);
        pcl::copyPointCloud(*blob, *_model[i]);
        _features.extraction(blob);

        _model_keypoints[i] = _features.keypoints;
        _model_normals[i] = _features.normals;
        _model_description[i] = _features.descriptor;
        _model_board[i] = _features.board;
    }
}

void nimbus::Recognition::correspondences(const pcl::PointCloud<pcl::PointXYZI>::ConstPtr blob)
{
    // Call model contructor before this.
    _features.extraction(blob);
    model_scene_corr.resize(_model_description.size());
    for(int j = 0; j < _model_description.size(); j++)
    {
        model_scene_corr[j].reset(new pcl::Correspondences());
        pcl::KdTreeFLANN<pcl::SHOT352> match_search;
        match_search.setInputCloud(_model_description[j]);
        for(std::size_t i = 0; i < _features.descriptor->size(); i++)
        {
            std::vector<int> neigh_indices(1);
            std::vector<float> neigh_sqrt_distance(1);

            if(! std::isfinite(_features.descriptor->at(i).descriptor[0])){
                continue;
            }

            int found_neighs = match_search.nearestKSearch(_features.descriptor->at(i), 1, neigh_indices, neigh_sqrt_distance);
            if(found_neighs == 1 && neigh_sqrt_distance[0] < 0.25f)
            {
                pcl::Correspondence corr(neigh_indices[0], static_cast<int> (i), neigh_sqrt_distance[0]);
                model_scene_corr[j]->push_back(corr);
            }
        }
        // std::cout << "Correspondences found at: " << j << "= " << model_scene_corr[j]->size () << std::endl;
    }
}

void nimbus::Recognition::cloudHough3D(const pcl::PointCloud<pcl::PointXYZI>::ConstPtr blob)
{
    pcl::PointCloud<pcl::PointXYZI>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZI>());
    pcl::copyPointCloud(*blob, *cloud);
    this->correspondences(cloud);

    std::vector<Eigen::Matrix4f, Eigen::aligned_allocator<Eigen::Matrix4f> > trasformations;
    std::vector<pcl::Correspondences> clusters;

    std::vector<Eigen::Matrix4f, Eigen::aligned_allocator<Eigen::Matrix4f> > rototranslations;
    std::vector<pcl::Correspondences> clustered_corrs;

    pcl::Hough3DGrouping<pcl::PointXYZI, pcl::PointXYZI, pcl::ReferenceFrame, pcl::ReferenceFrame> clusterer;
    for (int i = 0; i < 10; i++)
    {
        clusterer.setHoughBinSize (0.017);
        clusterer.setHoughThreshold (2.2);
        clusterer.setUseInterpolation (true);
        clusterer.setUseDistanceWeight (false);

        clusterer.setInputCloud (_model_keypoints[i]);
        clusterer.setInputRf (_model_board[i]);
        clusterer.setSceneCloud (_features.keypoints);
        clusterer.setSceneRf (_features.board);
        clusterer.setModelSceneCorrespondences (model_scene_corr[i]);

        clusterer.recognize (rototranslations, clustered_corrs);
        if (rototranslations.size() > 0){
            std::cout << "The model is recognized for Correspondences size: " <<  model_scene_corr[i]->size () << " at: " << i << std::endl;
            std::vector<pcl::PointCloud<pcl::PointXYZI>::ConstPtr> instances;
            for(std::size_t j = 0; j < rototranslations.size(); ++j)
            {
                std::vector<int> indices;
                pcl::PointCloud<pcl::PointXYZI>::Ptr rotated_model (new pcl::PointCloud<pcl::PointXYZI>());
                pcl::PointCloud<pcl::PointXYZI>::Ptr modelNoNaN (new pcl::PointCloud<pcl::PointXYZI>());
                pcl::removeNaNFromPointCloud(*_model[i], *modelNoNaN, indices);
                modelNoNaN->is_dense = false;
                pcl::transformPointCloud(*modelNoNaN, *rotated_model, rototranslations[j]);
                instances.push_back(rotated_model);
                trasformations.push_back(rototranslations[j]);
                clusters.push_back(clustered_corrs[j]);
            }
            this->registrationICP(instances, cloud, rototranslations, clusters);
        }
    }
}

void nimbus::Recognition::registrationICP (const std::vector<pcl::PointCloud<pcl::PointXYZI>::ConstPtr> instances,
                                           const pcl::PointCloud<pcl::PointXYZI>::ConstPtr scene,
                                           std::vector<Eigen::Matrix4f, Eigen::aligned_allocator<Eigen::Matrix4f> > rototranslations, 
                                           std::vector<pcl::Correspondences> clustered_corrs)
{
    ///////// ICP ////////////
    std::vector<Eigen::Matrix4f, Eigen::aligned_allocator<Eigen::Matrix4f>> rototransList;
    std::vector<pcl::PointCloud<pcl::PointXYZI>::ConstPtr> registered_instances;
    std::vector<int> indices;
    pcl::PointCloud<pcl::PointXYZI>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZI>());
    pcl::removeNaNFromPointCloud(*scene, *cloud, indices);
    cloud->is_dense = false;
    for(std::size_t i = 0; i < rototranslations.size(); ++i)
    {
        Eigen::Matrix4f icp_tf;
        Eigen::Matrix4f icp_final_tf;
        pcl::IterativeClosestPoint<pcl::PointXYZI, pcl::PointXYZI> icp;
        icp.setMaximumIterations(5);
        icp.setMaxCorrespondenceDistance(0.025f);
        icp.setTransformationEpsilon(1e-7);
        icp.setInputTarget(cloud);
        icp.setInputSource(instances[i]);
        pcl::PointCloud<pcl::PointXYZI>::Ptr registered (new pcl::PointCloud<pcl::PointXYZI>());
        icp.align(*registered);
        registered_instances.push_back(registered);
        icp_tf = icp.getFinalTransformation();
        // ToDo Why?
        icp_final_tf = icp_tf * rototranslations[i];
        // if(icp.hasConverged()){
        //     ROS_ERROR("!Alligned!");
        // }else{
        //     ROS_ERROR("!NOT Alligned!");
        // }
        rototransList.push_back(icp_final_tf);
    }
    this->hypothesisVerification(cloud, registered_instances, rototransList);
}

void nimbus::Recognition::hypothesisVerification(const pcl::PointCloud<pcl::PointXYZI>::ConstPtr blob,
                                                 std::vector<pcl::PointCloud<pcl::PointXYZI>::ConstPtr> registered_instances,
                                                 std::vector<Eigen::Matrix4f, Eigen::aligned_allocator<Eigen::Matrix4f> > rototranslations)
{
    std::vector<bool> mask;
    pcl::PointCloud<pcl::PointXYZI>::Ptr scene (new pcl::PointCloud<pcl::PointXYZI>);
    pcl::copyPointCloud(*blob, *scene);

    pcl::GlobalHypothesesVerification<pcl::PointXYZI, pcl::PointXYZI> GoHv;
    GoHv.setSceneCloud(scene);
    GoHv.addModels(registered_instances, true);
    GoHv.setResolution(0.005);
    GoHv.setOcclusionThreshold(0.01);
    GoHv.setInlierThreshold(0.005);
    GoHv.setOcclusionThreshold(0.01);
    GoHv.setRegularizer(3.0);
    GoHv.setRadiusClutter(0.03);
    GoHv.setClutterRegularizer(5.0);
    GoHv.setDetectClutter(true);
    GoHv.setRadiusNormals(0.05);
    GoHv.verify();
    GoHv.getMask(mask);
    this->publishPose(mask, rototranslations);
}

void nimbus::Recognition::publishPose(std::vector<bool> mask, std::vector<Eigen::Matrix4f, Eigen::aligned_allocator<Eigen::Matrix4f> > rototranslations)
{
    for(int i = 0; i < mask.size(); i++)
    {
        if(mask[i])
        {
            std::cout << "Instance " << i << " is GOOD! <---" << std::endl;
            Eigen::Matrix3f rotation = rototranslations[i].block<3,3>(0,0);
            Eigen::Vector3f translation = rototranslations[i].block<3,1>(0, 3);
            tf2::Matrix3x3 mat(rotation (0,0), rotation (0,1), rotation (0,2),
                               rotation (1,0), rotation (1,1), rotation (1,2),
                               rotation (2,0), rotation (2,1), rotation (2,2));
            tf2Scalar roll, pitch, yaw;
            mat.getEulerYPR(yaw, pitch, roll);
            tf2::Quaternion q;
            q.setRPY(roll, pitch, yaw);
            ROS_WARN("Position X: %f, Y: %f Z: %f", (float)translation(0), (float)translation(1), (float)translation(2));
            ROS_WARN("Rotation Roll: %f, Pitch: %f yaw: %f", (float)roll * (180 / M_PI), (float)pitch * (180 / M_PI), (float)yaw * (180 / M_PI));
            pose.header.frame_id = "camera";
            pose.child_frame_id = "object";
            pose.header.stamp = ros::Time::now();
            pose.transform.translation.x = translation(0);
            pose.transform.translation.y = translation(1);
            pose.transform.translation.z = translation(2);
            pose.transform.rotation = tf2::toMsg(q);
            pubPose.publish(pose);
            tfb.sendTransform(pose);
            ros::Duration(5.0).sleep();
        }
        else{
            // std::cout << "Instance " << i << " is bad!" << std::endl;
        }
    }
}

void nimbus::Recognition::visualization (const int num, 
                    const pcl::PointCloud<pcl::PointXYZI>::Ptr  scene,
                    std::vector<Eigen::Matrix4f, Eigen::aligned_allocator<Eigen::Matrix4f> > rototranslations,
                    std::vector<pcl::Correspondences> clustered_corrs){
    pcl::visualization::PCLVisualizer viewer("Correspondence");
    viewer.addPointCloud<pcl::PointXYZI> (scene, "scene_cloud");

    pcl::PointCloud<pcl::PointXYZI>::Ptr off_scene_model (new pcl::PointCloud<pcl::PointXYZI> ());
    pcl::PointCloud<pcl::PointXYZI>::Ptr off_scene_model_keypoints (new pcl::PointCloud<pcl::PointXYZI> ());

    pcl::transformPointCloud(*_model[num], *off_scene_model, Eigen::Vector3f (-1,0,0), Eigen::Quaternionf (1, 0, 0, 0));
    pcl::transformPointCloud (*_model_keypoints[num], *off_scene_model_keypoints, Eigen::Vector3f (-1,0,0), Eigen::Quaternionf (1, 0, 0, 0));

    pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZI> off_scene_model_color_handler (off_scene_model, 255, 255, 128);
    viewer.addPointCloud (off_scene_model, off_scene_model_color_handler, "off_scene_model");

    pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZI> scene_keypoints_color_handler (_features.keypoints, 0, 0, 255);
    viewer.addPointCloud (_features.keypoints, scene_keypoints_color_handler, "scene_keypoints");
    viewer.setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 5, "scene_keypoints");

    pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZI> off_scene_model_keypoints_color_handler (off_scene_model_keypoints, 0, 0, 255);
    viewer.addPointCloud (off_scene_model_keypoints, off_scene_model_keypoints_color_handler, "off_scene_model_keypoints");
    viewer.setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 5, "off_scene_model_keypoints");

    for(std::size_t i = 0; i < rototranslations.size(); i++)
    {
        pcl::PointCloud<pcl::PointXYZI>::Ptr rotated_model (new pcl::PointCloud<pcl::PointXYZI>());
        pcl::transformPointCloud (*_model[num], *rotated_model, rototranslations[i]);

        std::stringstream ss_cloud;
        ss_cloud << "instance" << i;

        pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZI> rotated_model_color_handler (rotated_model, 255, 0, 0);
        viewer.addPointCloud (rotated_model, rotated_model_color_handler, ss_cloud.str ());

        for (std::size_t j = 0; j < clustered_corrs[i].size (); ++j)
        {
            std::stringstream ss_line;
            ss_line << "correspondence_line" << i << "_" << j;
            pcl::PointXYZI& model_point = off_scene_model_keypoints->at (clustered_corrs[i][j].index_query);
            pcl::PointXYZI& scene_point = _features.keypoints->at (clustered_corrs[i][j].index_match);

            //  We are drawing a line for each pair of clustered correspondences found between the model and the scene
            viewer.addLine<pcl::PointXYZI, pcl::PointXYZI> (model_point, scene_point, 0, 255, 0, ss_line.str ());
        }
    }
    viewer.spinOnce(10000, false);
}