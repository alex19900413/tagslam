/* -*-c++-*--------------------------------------------------------------------
 * 2019 Bernd Pfrommer bernd.pfrommer@gmail.com
 */

#include "tagslam/graph.h"
#include "tagslam/pose_with_noise.h"
#include "tagslam/factor/absolute_pose_prior.h"
#include "tagslam/factor/relative_pose_prior.h"
#include "tagslam/factor/tag_projection.h"
#include "tagslam/value/pose.h"
#include "tagslam/optimizer.h"

#include <boost/range/irange.hpp>
#include <boost/graph/breadth_first_search.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/graph/graph_utility.hpp>
#include <vector>
#include <fstream>
#include <queue>
#include <map>
#include <sstream>


namespace tagslam {

  using boost::irange;

  static Graph::Id make_id(const ros::Time &t, const std::string &name) {
    return (name + "_" + std::to_string(t.toNSec()));
  }

  template <class G>
  class LabelWriter {
  public:
    LabelWriter(const G &g) : graph(&g) { }
    template <class VertexOrEdge>
    void operator()(std::ostream &out, const VertexOrEdge& v) const {
      out << "[label=\"" << (*graph)[v].vertex->getLabel() << "\", shape="
          << (*graph)[v].vertex->getShape() <<"]";
    }
  private:
    const G *graph;
  };

  template<class G>
  static void plot(const std::string &fname, const G &graph) {
    std::ofstream ofile(fname);
    boost::write_graphviz(ofile, graph, LabelWriter<G>(graph));
  }
/*
  class demo_visitor : public boost::default_bfs_visitor {
  public:
    template <typename Vertex, typename Graph>
    void discover_vertex(Vertex u, Graph &g) {
      printf("Visited vertex %lu with name %s\n",   u, g[u].name.c_str());
    };
  };
*/
  Graph::Graph() {
#if 0    
    std::vector<BoostGraphVertex> vertices;
    for (const int i: irange(0, 10)) {
      BoostGraphVertex v =
        boost::add_vertex(Vertex("name_" + std::to_string(i)), graph_);
      vertices.push_back(v);
    }
    for (const int i: irange(1, 5)) {
      boost::add_edge(vertices[0], vertices[i], graph_);
    }
    for (const int i: irange(5, 10)) {
      boost::add_edge(vertices[4], vertices[i], graph_);
    }
    demo_visitor vis;
    //boost::breadth_first_search(graph_, vertices[4], boost::visitor(vis));

#endif
  }

/*
  template <class G>
  struct UnoptimizedValuesPredicate {
    UnoptimizedValuesPredicate() : graph(NULL) {}
    UnoptimizedValuesPredicate(const G &g) : graph(&g) {}
    template <typename V>
    bool operator()(const V &v) const {
      return (!(*graph)[v].vertex->getIsOptimized());
    }
    const G *graph;
  };


  class SubGraphMaker {
  public:
    typedef std::pair<BoostGraphVertex,
                      BoostGraphVertex> QueueEntry;
    typedef std::queue<QueueEntry> Queue;
    typedef BoostGraph::out_edge_iterator OutEdgeIterator;
    SubGraphMaker(const BoostGraph &graph, BoostGraph *sg) :
      graph_(graph), subGraph_(sg) {}

    void add(const std::vector<BoostGraphVertex> &startVertices) {
      Queue().swap(openQueue_); // clears queue
      foundVertices_.clear();
      foundEdges_.clear();
      for (const auto &startVertex: startVertices) {
        add(startVertex);
      }
    }

    void add(const BoostGraphVertex &start) {
      if (foundVertices_.count(start) != 0) {
        return; // already in graph, we're done
      }
      // add start vertex to subgraph and push it into the queue
      BoostGraphVertex u = boost::add_vertex(graph_[start], *subGraph_);
      vertexMap_[start] = u;
      // have to remember vertices in both graphs to be
      // able to later make the edges
      openQueue_.push(QueueEntry(start, u));
      while (!openQueue_.empty()) {
        Queue::value_type q = openQueue_.front();
        // iterate over all adjacent vertices, insert them
        // in the subgraph and to the back of the queue
        OutEdgeIterator it, itEnd;
        std::tie(it, itEnd) = boost::out_edges(q.first, graph_);
        for (; it != itEnd; ++it) {
          // can do more filtering here if desired
          BoostGraphVertex src(boost::source(*it, graph_)),
            targ(boost::target(*it, graph_));
          
          if (foundVertices_.count(targ) == 0) {
            BoostGraphVertex v = boost::add_vertex(graph_[targ], *subGraph_);
            vertexMap_[targ] = v;
            openQueue_.push(QueueEntry(src, targ));
            foundVertices_.insert(targ);
          }
          // at this point we can be sure that both source and target
          // vertices are in the subgraph
          const auto &st = vertexMap_[targ];
          if (!boost::edge(q.second, st, *subGraph_).second) {
            boost::add_edge(q.second, st, graph_[*it], *subGraph_);
          }
        }
        openQueue_.pop();
      }
    }
  private:
    const BoostGraph                       &graph_;
    BoostGraph                             *subGraph_;
    Queue                                   openQueue_;
    std::set<BoostGraphVertex> foundVertices_;
    std::map<BoostGraphVertex, BoostGraphVertex> vertexMap_;
    std::set<BoostGraph::edge_descriptor>   foundEdges_;

  };

*/  

  void Graph::optimize() {
    if (optimizer_) {
      if (optimizeFullGraph_) {
        optimizer_->optimizeFullGraph();
      } else {
        optimizer_->optimize();
      }
    }
  }

  void Graph::addBody(const Body &body) {
    // add body pose as vertex
    if (body.isStatic()) {
      const ros::Time t0(0);
      if (body.getPoseWithNoise().isValid()) {
        Graph::VertexPose vp = 
          addPoseWithPrior(t0, body_name(body.getName()),
                           body.getPoseWithNoise());
      }
    } 
    // add associated tags as vertices
    for (const auto &tag: body.getTags()) {
      addTag(*tag);
    }
    ROS_INFO_STREAM("added body " << body.getName() << " with "
                    << body.getTags().size() << " tags");
  }

  Graph::VertexPose
  Graph::addTag(const Tag2 &tag) {
    const string name = tag_name(tag.getId());
    const ros::Time t0(0);
    if (tag.getPoseWithNoise().isValid()) {
      return (addPoseWithPrior(t0, name, tag.getPoseWithNoise()));
    } else {
      return (addPose(t0, name, tag.getPoseWithNoise().getPose(), false));
    }
  }

  bool Graph::getPose(const ros::Time &t, const string &name,
                      Transform *tf) const {
    VertexPose vp = findPose(t, name);
    if (vp.pose && vp.pose->isOptimized()) {
      *tf = optimizer_->getPose(vp.pose->getKey());
      return (true);
    }
    return (false);
  }

  Graph::VertexPose
  Graph::findPose(const ros::Time &t, const string &name) const {
    const auto it = idToVertex_.find(make_id(t, name));
    if (it == idToVertex_.end()) {
      return (VertexPose()); // not found, return empty vp
    }
    const GraphVertex &v = graph_[it->second];
    std::shared_ptr<value::Pose> p =
      std::dynamic_pointer_cast<value::Pose>(v.vertex);
    if (!p) {
      ROS_ERROR_STREAM("vertex for id " << name << " is no pose!");
      return (VertexPose());
    }
    return (VertexPose(it->second, p));
  }

  void
  Graph::test() {
  }


  BoostGraphVertex
  Graph::addPrior(const ros::Time &t, const VertexPose &vp,
                  const string &name,
                  const PoseWithNoise &pn) {
    if (!vp.pose->isOptimized()) {
      vp.pose->setPose(pn.getPose());
      vp.pose->setKey(optimizer_->addPose(pn.getPose()));
    }
    std::shared_ptr<factor::AbsolutePosePrior>
      fac(new factor::AbsolutePosePrior(t, pn, name));
    // add factor to optimizer
    fac->setKey(optimizer_->addAbsolutePosePrior(vp.pose->getKey(), pn));
    // add factor vertex to boost graph
    BoostGraphVertex fv = boost::add_vertex(GraphVertex(fac), graph_);
    // now add edge between factor and value
    boost::add_edge(fv, vp.vertex, GraphEdge(0), graph_);
    return (fv);
  }
  
  Graph::VertexPose
  Graph::addPoseWithPrior(const ros::Time &t, const string &name,
                          const PoseWithNoise &pn) {
    // add pose into boost graph
    VertexPose vp = addPose(t, name, pn.getPose(), true);
    // add pose value to optimizer
    vp.pose->setKey(optimizer_->addPose(vp.pose->getPose()));

    addPrior(t, vp, name, pn);
    return (vp);
  }

  Graph::VertexPose
  Graph::addPose(const ros::Time &t, const string &name,
                 const Transform &pose, bool poseIsValid) {
    Id id = make_id(t, name);
    if (hasId(id)) {
      ROS_ERROR_STREAM("duplicate pose added, id: " << id);
      throw std::runtime_error("duplicate pose added!");
    }

    PoseValuePtr np(new value::Pose(t, pose, name, poseIsValid));
    // add new pose value to graph
    const BoostGraphVertex npv = boost::add_vertex(GraphVertex(np), graph_);
    idToVertex_.insert(IdToVertexMap::value_type(id, npv));
    return (VertexPose(npv, np));
  }

  void
  Graph::addBodyPoseDelta(const ros::Time &tPrev, const ros::Time &tCurr,
                          const BodyConstPtr &body,
                          const PoseWithNoise &deltaPose) {
    Transform prevPose;
    string name   = body_name(body->getName());
    VertexPose pp = findPose(tPrev, name);
    VertexPose cp = findPose(tCurr, name);
    if (!pp.pose) {
      ROS_ERROR_STREAM("no prev pose for delta: " << name << " " << tPrev);
      throw std::runtime_error("no prev pose for delta");
    }
 
    const Transform newPose = deltaPose.getPose() * pp.pose->getPose();
    bool poseValid = pp.pose->isValid();
    // add current body pose if not already there
    if (!cp.pose) {
      std::cout << "adding current pose!" << std::endl;
      // nothing there at all!
      cp = addPose(tCurr, name, newPose, poseValid);
    } else if (!cp.pose->isValid() && poseValid) {
      // the current pose is invalid, let's fix that
      cp.pose->setPose(newPose);
    }
    if (!cp.pose->isOptimized() && cp.pose->isValid()) {
      // not part of the optimizer yet, add it
      cp.pose->setKey(optimizer_->addPose(newPose));
    }

    addRelativePosePrior(tCurr, "bodyrel:" + body->getName(), pp, cp, deltaPose,
                         cp.pose->isOptimized() && pp.pose->isOptimized());
  }

  BoostGraphVertex
  Graph::addRelativePosePrior(const ros::Time &t,
                              const string &name,
                              const VertexPose &vp1,
                              const VertexPose &vp2,
                              const PoseWithNoise &deltaPose,
                              bool addToOptimizer) {
    
    // add new factor and its edges to graph
    VertexPtr fvv(new factor::RelativePosePrior(t, deltaPose, name));
    const BoostGraphVertex fv = boost::add_vertex(GraphVertex(fvv), graph_);
    boost::add_edge(fv, vp1.vertex, GraphEdge(0), graph_);
    boost::add_edge(fv, vp2.vertex, GraphEdge(1), graph_);

    if (addToOptimizer) {
      optimizer_->addRelativePosePrior(vp1.pose->getKey(), vp2.pose->getKey(),
                                       deltaPose);
    }
    return (fv);
  }

  BoostGraphVertex
  Graph::addProjectionFactor(const ros::Time &t,
                             const Tag2ConstPtr &tag,
                             const Camera2ConstPtr &cam,
                             const geometry_msgs::Point *imgCorners) {
    BoostGraphVertex v;
    // connect: tag_body_pose, tag_pose, cam_pose, rig_pose
    VertexPose tagvp = findPose(ros::Time(0),
                                Graph::tag_name(tag->getId()));
    if (tagvp.pose == NULL) {
      ROS_ERROR_STREAM("no pose found for tag: " << tag->getId());
      throw std::runtime_error("no tag pose found!");
    }
    BodyConstPtr body = tag->getBody();
    VertexPose bodyvp = findPose(body->isStatic() ? ros::Time(0) : t,
                                 Graph::body_name(body->getName()));
    if (bodyvp.pose == NULL) {
      ROS_ERROR_STREAM("no pose found for body: " << body->getName());
      throw std::runtime_error("no body pose found!");
    }
    BodyConstPtr rig = cam->getRig();
    VertexPose rigvp = findPose(rig->isStatic() ? ros::Time(0) : t,
                                Graph::body_name(rig->getName()));
    if (rigvp.pose == NULL) {
      ROS_ERROR_STREAM("no pose found for rig: " << rig->getName());
      throw std::runtime_error("no rig pose found!");
    }
    VertexPose camvp = findPose(ros::Time(0), Graph::cam_name(cam->getName()));
    if (camvp.pose == NULL) {
      ROS_ERROR_STREAM("no pose found for cam: " << cam->getName());
      throw std::runtime_error("no cam pose found!");
    }

    VertexPtr pjfac(
      new factor::TagProjection(t, cam, tag, imgCorners, cam->getName() +
                                "-" + std::to_string(tag->getId())));
    v = boost::add_vertex(GraphVertex(pjfac), graph_);
    boost::add_edge(v, tagvp.vertex,  GraphEdge(0), graph_);
    boost::add_edge(v, bodyvp.vertex, GraphEdge(1), graph_);
    boost::add_edge(v, rigvp.vertex,  GraphEdge(2), graph_);
    boost::add_edge(v, camvp.vertex,  GraphEdge(3), graph_);
    
    return (v);
  }


  void Graph::plotDebug(const ros::Time &t, const string &tag) {
    std::stringstream ss;
    ss << tag << "_" <<  t.toNSec() << ".dot";
    plot(ss.str(), graph_);
  }

  // static method!
  std::string Graph::tag_name(int tagid) {
    return (string("tag:") + std::to_string(tagid));
  }
  // static method!
  std::string Graph::body_name(const string &body) {
    return ("body:" + body);
  }
  // static method!
  std::string Graph::cam_name(const string &cam) {
    return ("cam:" + cam);
  }
}  // end of namespace
