/*
 * Copyright 2009 The VOTCA Development Team (http://www.votca.org)
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
#include <votca/csg/csgapplication.h>
#include <votca/tools/average.h>
#include <votca/tools/tokenizer.h>

// using namespace votca::tools;
using namespace std;
using namespace votca::csg;

class CsgTestApp : public CsgApplication {
  string ProgramName() { return "radii"; }
  void HelpText(ostream &out) {
    out << "calculate gyration- and hydrodynamic radius for a specific "
           "molecule or molecule type";
  }

  // some program options are added here
  void Initialize();

  // we want to process a trajectory
  bool DoTrajectory() { return true; }

  // write out results in EndEvaluate
  void EndEvaluate();
  // do calculation in this function
  void EvalConfiguration(Topology *top, Topology *top_ref);

 protected:
  // inverse hydrodynamic radius average
  Average<double> _inv_r_hydr;
  // radius of gyration squared average
  Average<double> _r_gyr_sq;
  // mass weighted radius of gyration squared average
  Average<double> _r_gyr_m_sq;
};

int main(int argc, char **argv) {
  CsgTestApp app;

  return app.Exec(argc, argv);
}

void CsgTestApp::EvalConfiguration(Topology *top, Topology *top_ref) {
  // loop over all molecules
  for (MoleculeContainer::iterator imol = top->Molecules().begin();
       imol != top->Molecules().end(); ++imol) {
    Molecule *mol = *imol;
    // does the id match if given?
    if (OptionsMap().count("mol")) {
      if (OptionsMap()["mol"].as<int>() != mol->getId() + 1) continue;
    }
    // otherwise does the name pattern match?
    else if (!wildcmp(OptionsMap()["molname"].as<string>().c_str(),
                      mol->getName().c_str()))
      continue;  // if not skip this molecule

    // Number of beads in the molecule
    int N = mol->BeadCount();

    // sqared tensor of gyration for current snapshot
    double r_gyr_sq = 0;
    // inverse hydrodynamic radius for current snapshot
    double inv_r_hydr = 0;

    // loop over all bead pairs in molecule
    for (int i = 0; i < N; ++i) {
      for (int j = i + 1; j < N; ++j) {
        // distance between bead i and j
        vec r_ij = mol->getBead(i)->getPos() - mol->getBead(j)->getPos();
        // radius of gyration squared
        r_gyr_sq += r_ij * r_ij / (double)(N * N);
        // hydrodynamic radius
        inv_r_hydr += 2. / (abs(r_ij) * (double(N * N)));
      }
    }

    // add calculated values to the averages
    _r_gyr_sq.Process(r_gyr_sq);
    _inv_r_hydr.Process(inv_r_hydr);

    // calculate the mass weighted tensor of gyration
    // first calculate mass + center of mass
    double M = 0;
    vec cm(0, 0, 0);
    for (int i = 0; i < N; ++i) {
      M += mol->getBead(i)->getM();
      cm += mol->getBead(i)->getPos() * mol->getBead(i)->getM();
    }
    cm /= M;
    // now tensor of gyration based on cm
    double r_gyr_m_sq = 0;
    for (int i = 0; i < N; ++i) {
      vec r_ij = mol->getBead(i)->getPos() - cm;
      r_gyr_m_sq += mol->getBead(i)->getM() * (r_ij * r_ij);
    }
    r_gyr_m_sq /= M;

    // add to average
    _r_gyr_m_sq.Process(r_gyr_m_sq);
  }
}

// output everything when processing frames is done
void CsgTestApp::EndEvaluate() {
  cout << "\n\n------------------------------\n";
  cout << "radius of gyration:                  " << sqrt(_r_gyr_sq.getAvg())
       << endl;
  cout << "mass weighted radius of gyration:    " << sqrt(_r_gyr_m_sq.getAvg())
       << endl;
  cout << "hydrodynamic radius:                 " << 1. / _inv_r_hydr.getAvg()
       << endl;
  cout << "------------------------------\n";
}

// add our user program options
void CsgTestApp::Initialize() {
  CsgApplication::Initialize();
  // add program option to pick molecule
  AddProgramOptions("Molecule filter options")(
      "mol", boost::program_options::value<int>(), "molecule number")(
      "molname", boost::program_options::value<string>()->default_value("*"),
      "pattern for molecule name");
}
