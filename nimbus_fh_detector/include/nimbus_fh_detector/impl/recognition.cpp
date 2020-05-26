
#include <nimbus_fh_detector/recognition.hpp>

#include <pcl/io/pcd_io.h>
#include <pcl/common/transforms.h>
#include <pcl/recognition/cg/hough_3d.h>

nimbus::Recognition::Recognition(ros::NodeHandle nh, const std::string path): _path(path), _features(nh, 0.01, 0.01, 0.01)
{

}
nimbus::Recognition::~Recognition(){}

void nimbus::Recognition::constructModelParam()
{
    _model_normals.resize(10);
    _model_keypoints.resize(10);
    _model_description.resize(10);
    _model_board.resize(10);
    // ToDo: Change the fixed number of *.pcd file to for loop
    std::stringstream model_path;
    model_path << _path << "/box_";
    for(int i = 0; i < 10; i++)
    {
        _model_normals[i].reset(new pcl::PointCloud<pcl::Normal>());
        _model_keypoints[i].reset(new pcl::PointCloud<pcl::PointXYZI>());
        _model_description[i].reset(new pcl::PointCloud<pcl::SHOT352>());
        _model_board[i].reset(new pcl::PointCloud<pcl::ReferenceFrame>());

        typename pcl::PointCloud<pcl::PointXYZI>::Ptr blob (new pcl::PointCloud<pcl::PointXYZI>());
        model_path << std::to_string(i) << ".pcd";
        pcl::io::loadPCDFile(model_path.str(), *blob);
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
    for(int i; i < _model_description.size(); i++)
    {
        model_scene_corr[i].reset(new pcl::Correspondences());
        pcl::KdTreeFLANN<pcl::SHOT352> match_search;
        match_search.setInputCloud(_model_description[i]);
        for(std::size_t i; i < _features.descriptor->size(); i++)
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
                model_scene_corr[i]->push_back(corr);
            }
        }
    }
}

void nimbus::Recognition::cloudHough3D(const pcl::PointCloud<pcl::PointXYZI>::ConstPtr blob,
                                       std::vector<Eigen::Matrix4f, Eigen::aligned_allocator<Eigen::Matrix4f> > &rototranslations, 
                                       std::vector<pcl::Correspondences> &clustered_corrs)
{
    this->correspondences(blob);
    pcl::Hough3DGrouping<pcl::PointXYZI, pcl::PointXYZI, pcl::ReferenceFrame, pcl::ReferenceFrame> clusterer;
    clusterer.setHoughBinSize (0.017);
    clusterer.setHoughThreshold (4.0);
    clusterer.setUseInterpolation (true);
    clusterer.setUseDistanceWeight (false);

    clusterer.setInputCloud (_model_keypoints[0]);
    clusterer.setInputRf (_model_board[0]);
    clusterer.setSceneCloud (_features.keypoints);
    clusterer.setSceneRf (_features.board);
    clusterer.setModelSceneCorrespondences (model_scene_corr[0]);

    clusterer.recognize (rototranslations, clustered_corrs);
}