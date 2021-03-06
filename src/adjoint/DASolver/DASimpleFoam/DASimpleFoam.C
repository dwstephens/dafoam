/*---------------------------------------------------------------------------*\

    DAFoam  : Discrete Adjoint with OpenFOAM
    Version : v2

\*---------------------------------------------------------------------------*/

#include "DASimpleFoam.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

defineTypeNameAndDebug(DASimpleFoam, 0);
addToRunTimeSelectionTable(DASolver, DASimpleFoam, dictionary);
// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

DASimpleFoam::DASimpleFoam(
    char* argsAll,
    PyObject* pyOptions)
    : DASolver(argsAll, pyOptions),
      simplePtr_(nullptr),
      pPtr_(nullptr),
      UPtr_(nullptr),
      phiPtr_(nullptr),
      laminarTransportPtr_(nullptr),
      turbulencePtr_(nullptr),
      daTurbulenceModelPtr_(nullptr),
      daFvSourcePtr_(nullptr),
      fvSourcePtr_(nullptr)
{
}
// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

void DASimpleFoam::initSolver()
{
    /*
    Description:
        Initialize variables for DASolver
    */

    Info << "Initializing fields for DASimpleFoam" << endl;
    Time& runTime = runTimePtr_();
    fvMesh& mesh = meshPtr_();
#include "createSimpleControlPython.H"
#include "createFieldsSimple.H"
#include "createAdjointIncompressible.H"
    // initialize checkMesh
    daCheckMeshPtr_.reset(new DACheckMesh(runTime, mesh));

    // initialize fvSource and compute the source term
    const dictionary& allOptions = daOptionPtr_->getAllOptions();
    if (allOptions.subDict("fvSource").toc().size() != 0)
    {
        Info << "Computing fvSource" << endl;
        word sourceName = allOptions.subDict("fvSource").toc()[0];
        word fvSourceType = allOptions.subDict("fvSource").subDict(sourceName).getWord("type");
        daFvSourcePtr_.reset(DAFvSource::New(
            fvSourceType, mesh, daOptionPtr_(), daModelPtr_(), daIndexPtr_()));
        daFvSourcePtr_->calcFvSource(fvSource);
    }
}

label DASimpleFoam::solvePrimal(
    const Vec xvVec,
    Vec wVec)
{
    /*
    Description:
        Call the primal solver to get converged state variables

    Input:
        xvVec: a vector that contains all volume mesh coordinates

    Output:
        wVec: state variable vector
    */

#include "createRefsSimple.H"

    // first check if we need to change the boundary conditions based on
    // the primalBC dict in DAOption. NOTE: this will overwrite whatever
    // boundary conditions defined in the "0" folder
    dictionary bcDict = daOptionPtr_->getAllOptions().subDict("primalBC");
    if (bcDict.toc().size() != 0)
    {
        Info << "Setting up primal boundary conditions based on pyOptions: " << endl;
        daFieldPtr_->setPrimalBoundaryConditions();
    }

    turbulencePtr_->validate();

    Info << "\nStarting time loop\n"
         << endl;

    // deform the mesh based on the xvVec
    this->pointVec2OFMesh(xvVec);

    // check mesh quality
    label meshOK = this->checkMesh();

    if (!meshOK)
    {
        return 1;
    }

    label nSolverIters = 1;
    primalMinRes_ = 1e10;
    label printInterval = daOptionPtr_->getOption<label>("printInterval");
    while (this->loop(runTime)) // using simple.loop() will have seg fault in parallel
    {
        if (nSolverIters % printInterval == 0 || nSolverIters == 1)
        {
            Info << "Time = " << runTime.timeName() << nl << endl;
        }

        p.storePrevIter();

        // --- Pressure-velocity SIMPLE corrector
        {
#include "UEqnSimple.H"
#include "pEqnSimple.H"
        }

        laminarTransport.correct();
        daTurbulenceModelPtr_->correct();

        if (nSolverIters % printInterval == 0 || nSolverIters == 1)
        {
            daTurbulenceModelPtr_->printYPlus();
            
            this->printAllObjFuncs();

            Info << "ExecutionTime = " << runTime.elapsedCpuTime() << " s"
                 << "  ClockTime = " << runTime.elapsedClockTime() << " s"
                 << nl << endl;
        }

        runTime.write();

        nSolverIters++;
    }

    this->calcPrimalResidualStatistics("print");

    // primal converged, assign the OpenFoam fields to the state vec wVec
    this->ofField2StateVec(wVec);

    // write the mesh to files
    mesh.write();

    Info << "End\n"
         << endl;

    return this->checkResidualTol();
}

} // End namespace Foam

// ************************************************************************* //
