/*
 * Copyright 2009-2018 The VOTCA Development Team (http://www.votca.org)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdlib.h>
#include <votca/csg/beadlist.h>
#include <votca/csg/csgapplication.h>
#include <votca/csg/nblist.h>
#include <votca/csg/nblistgrid.h>
#include <votca/tools/histogramnew.h>

using namespace std;
using namespace votca::csg;

class OrientCorrApp : public CsgApplication {

  string ProgramName() { return "orientcorr"; }

  void HelpText(ostream &out) {
    out << "Calculates the orientational correlation function\n"
           "    <3/2*u(0)*u(r) - 1/2>\n"
           "for a polymer melt, where u is the vector pointing along a bond "
           "and \n"
           "r the distance between bond segments (centered on middle of "
           "bond).\n\n"
           "The output is correlation.dat (with intra-molecular contributions) "
           "and\n"
           "correlation_excl.dat, where inter-molecular contributions are "
           "excluded.";
  }

  void Initialize();

  bool DoTrajectory() { return true; }
  // do a threaded analyzis, splitting in time domain
  bool DoThreaded() { return true; }
  // we don't care about the order of the analyzed frames
  bool SynchronizeThreads() { return false; }

  // called before groing through all frames
  void BeginEvaluate(CSG_Topology *top, CSG_Topology *top_ref);
  // called after all frames were parsed
  void EndEvaluate();

  // creates a worker for a thread
  CsgApplication::Worker *ForkWorker(void);
  // merge data of worker into main
  void MergeWorker(Worker *worker);

 public:
  // helper class to choose nbsearch algorithm
  static NBList *CreateNBSearch();

 protected:
  HistogramNew _cor;
  HistogramNew _count;
  HistogramNew _cor_excl;
  HistogramNew _count_excl;
  static string _nbmethod;
  double _cut_off;
  int _nbins;
};

string OrientCorrApp::_nbmethod;

// Earch thread has a worker and analysis data
class MyWorker : public CsgApplication::Worker {
 public:
  ~MyWorker(){};

  // evaluate the current frame
  void EvalConfiguration(CSG_Topology *top, CSG_Topology *top_ref);

  // callback if neighborsearch finds a pair
  bool FoundPair(Bead *b1, Bead *b2, const vec &r, const double dist);

  // accumulator of the 3/2*u(0)u(r) - 1/2
  HistogramNew _cor;
  // number of hits for each bin
  HistogramNew _count;
  // accumulator of the 3/2*u(0)u(r) - 1/2, only inter-molecular
  HistogramNew _cor_excl;
  // number of hits for each bin, only inter-molecular
  HistogramNew _count_excl;
  double _cut_off;
};

int main(int argc, char **argv) {
  OrientCorrApp app;
  return app.Exec(argc, argv);
}

// add some program options
void OrientCorrApp::Initialize() {
  // add all standard application options
  CsgApplication::Initialize();
  // some application specific options
  AddProgramOptions("Neighbor search options")(
      "cutoff,c",
      boost::program_options::value<double>(&_cut_off)->default_value(1.0),
      "cutoff for the neighbor search")(
      "nbins", boost::program_options::value<int>(&_nbins)->default_value(40),
      "number of bins for the grid")(
      "nbmethod",
      boost::program_options::value<string>(&_nbmethod)->default_value("grid"),
      "neighbor search algorithm (simple or grid)");
}

NBList *OrientCorrApp::CreateNBSearch() {
  if (_nbmethod == "simple") return new NBList();
  if (_nbmethod == "grid") return new NBListGrid();

  throw std::runtime_error(
      "unknown neighbor search method, use simple or grid");
  return NULL;
}

// initialize the histograms
void OrientCorrApp::BeginEvaluate(CSG_Topology *top, CSG_Topology *top_ref) {
  _cor.Initialize(0, _cut_off, _nbins);
  _count.Initialize(0, _cut_off, _nbins);
  _cor_excl.Initialize(0, _cut_off, _nbins);
  _count_excl.Initialize(0, _cut_off, _nbins);
}

// creates worker for each thread
CsgApplication::Worker *OrientCorrApp::ForkWorker() {
  MyWorker *worker;
  worker = new MyWorker();
  worker->_cut_off = _cut_off;
  worker->_cor.Initialize(0, worker->_cut_off, _nbins);
  worker->_count.Initialize(0, worker->_cut_off, _nbins);
  worker->_cor_excl.Initialize(0, worker->_cut_off, _nbins);
  worker->_count_excl.Initialize(0, worker->_cut_off, _nbins);
  return worker;
}

// evaluates a frame
void MyWorker::EvalConfiguration(CSG_Topology *top, CSG_Topology *top_ref) {

  // first genearate a mapped topology
  // the beads are sitting on the bonds and have an orientation which
  // is pointing along bond direction
  CSG_Topology mapped;
  cout << "generating mapped topology...";

  // copy box size
  mapped.setBox(top->getBox());

  // loop over all molecules
  for (MoleculeContainer::iterator iter = top->Molecules().begin();
       iter != top->Molecules().end(); ++iter) {
    Molecule *mol_src = *iter;
    // create a molecule in mapped topology
    Molecule *mol = mapped.CreateMolecule(mol_src->getName());
    // loop over beads in molecule
    for (int i = 0; i < mol_src->BeadCount() - 1; ++i) {
      // create a bead in mapped topology
      string bead_type = "A";
      if (mapped.BeadTypeExist(bead_type) == false) {
        mapped.RegisterBeadType(bead_type);
      }
      Bead *b = mapped.CreateBead(3, "A", bead_type, 1, 0.0, 0.0);
      vec p1 = mol_src->getBead(i)->getPos();
      vec p2 = mol_src->getBead(i + 1)->getPos();
      // position is in middle of bond
      vec pos = 0.5 * (p1 + p2);
      // orientation pointing along bond
      vec v = p2 - p1;
      v.normalize();
      b->setPos(pos);
      b->setV(v);
      mol->AddBead(b, "A");
    }
  }
  cout << "done\n";

  // the neighbor search only finds pairs, add self-self correlation parts here
  _cor.Process(0.0f, mapped.BeadCount());
  _count.Process(0.0f, mapped.BeadCount());

  // search for all beads
  BeadList b;
  b.Generate(mapped, "*");

  // create/initialize neighborsearch
  NBList *nb = OrientCorrApp::CreateNBSearch();
  nb->setCutoff(_cut_off);

  // set callback for each pair found
  nb->SetMatchFunction(this, &MyWorker::FoundPair);

  // execute the search
  nb->Generate(b);

  delete nb;
}

// process a pair, since return value is falsed, pairs are not cached which
// saves a lot of memory for the big systems
bool MyWorker::FoundPair(Bead *b1, Bead *b2, const vec &r, const double dist) {
  double tmp = b1->getV() * b2->getV();
  double P2 = 3. / 2. * tmp * tmp - 0.5;

  // calculate average without exclusions
  _cor.Process(dist, P2);
  _count.Process(dist);

  if (b1->getMolecule() == b2->getMolecule()) return false;

  // calculate average with excluding intramolecular contributions
  _cor_excl.Process(dist, P2);
  _count_excl.Process(dist);

  return false;
}

// merge analysed data of a worker into main applications
void OrientCorrApp::MergeWorker(Worker *worker) {
  MyWorker *myWorker;
  myWorker = dynamic_cast<MyWorker *>(worker);
  _cor.data().y() = _cor.data().y() + myWorker->_cor.data().y();
  _count.data().y() = _count.data().y() + myWorker->_count.data().y();

  _cor_excl.data().y() = _cor_excl.data().y() + myWorker->_cor_excl.data().y();
  _count_excl.data().y() =
      _count_excl.data().y() + myWorker->_count_excl.data().y();
}

// write out the data
void OrientCorrApp::EndEvaluate() {
  _cor.data().y() = _cor.data().y().cwiseQuotient(_count.data().y());
  _cor.data().Save("correlation.dat");

  _cor_excl.data().y() =
      _cor_excl.data().y().cwiseQuotient(_count_excl.data().y());
  _cor_excl.data().Save("correlation_excl.dat");
}
