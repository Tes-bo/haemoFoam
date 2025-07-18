/*--------------------------------*- C++ -*----------------------------------*\
| =========                 |                                                 |
| \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox           |
|  \\    /   O peration     | Version:  v1812                                 |
|   \\  /    A nd           | Web:      www.OpenFOAM.com                      |
|    \\/     M anipulation  |                                                 |
\*---------------------------------------------------------------------------*/


/*---------------------------------------------------------------------------*\
Application
    haemoPimpleFoam

Description
    Transient solver for incompressible, turbulent flow
    of non-Newtonian high particle load suspension fluids, using
    the PIMPLE (merged PISO-SIMPLE) algorithm.

    Developed for modelling the shear thinning and hematocrit transport
    in blood flow.

    Uses particle transport
    equation based on (Phillips, Armstrong et al. 1992):

    PHILLIPS, R.J., ARMSTRONG, R.C., BROWN, R.A., GRAHAM, A.L. and
    * ABBOTT, J.R., 1992.
       A constitutive equation for concentrated suspensions that accounts for
       shear‐induced particle migration.
       Physics of Fluids A: Fluid Dynamics, 4(1), pp. 30-40.

    Turbulence modelling is generic, i.e. laminar, RAS or LES may be selected.
    * although particle transport model is not developed for turbulent
    * flow models.

    Consistent formulation without time-step and relaxation dependence by Jasak

Author and Copyright
    Dr Torsten Schenkel
    Department Engineering and Mathematics
    Material and Engineering Research Institute MERI
    Sheffield Hallam University
    February 2019
    All Rights Reserved
\*---------------------------------------------------------------------------*/

#include "fvCFD.H"
#include "singlePhaseTransportModel.H"
#include "pimpleControl.H"

#ifdef OPENFOAMESIORFOUNDATION
    #include "dynamicFvMesh.H"
    #include "turbulentTransportModel.H"
    #include "CorrectPhi.H"
    #include "fvOptions.H"
    #include "localEulerDdtScheme.H"
    #include "fvcSmooth.H"
    // Windkessel includes:
    #include "fixedFluxPressureFvPatchScalarField.H"
    #include "scalarIOList.H"
    #include "WKFunctions.C"   // Windkessel function file
    #include "WKBCFvPatchScalarField.H"
#else
    #include "turbulenceModel.H"
#endif
// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

int main(int argc, char *argv[])
{

#ifdef OPENFOAMESIORFOUNDATION
    #include "postProcess.H"
    
    #include "setRootCaseLists.H"
    #include "createTime.H"
    #include "createDynamicFvMesh.H"
    #include "initContinuityErrs.H"
    #include "createDyMControls.H"
    #include "createWindkessel.H"  //Windkessel header file (has to be placed in this order since p depends on scalar list storage)
    #include "createFields.H"
    #include "createUfIfPresent.H"
    #include "CourantNo.H"

#   ifdef OPENFOAMFOUNDATION
        #include "setInitialDeltaT.H"
#   endif

#else
    #include "setRootCase.H"
    #include "createTime.H"
    #include "createMesh.H"

    pimpleControl pimple(mesh);

    #include "createFields.H"
    #include "initContinuityErrs.H"
    #include "createTimeControls.H"
#endif

    // Read properties for haematocrit transp
    #include "readHaemoProperties.H"

#ifdef OPENFOAMFOUNDATION
    if (!LTS)
    {
        #include "CourantNo.H"
        #include "setInitialDeltaT.H"
    }

    turbulence->validate();
#else
    turbulence->correct();
#endif

    // Back to original code

    Info<< "\nStarting time loop\n" << endl;

    while (runTime.run())
    {
#ifdef OPENFOAMESIORFOUNDATION
        #include "readDyMControls.H"
#else   
        #include "readTimeControls.H"
#endif

#ifdef OPENFOAMFOUNDATION
        if (LTS)
        {
            #include "setRDeltaT.H"
        }
        else
        {
            #include "CourantNo.H"
            #include "setDeltaT.H"
        }
#else
        #include "CourantNo.H"
        #include "setDeltaT.H"
#endif
            
        runTime++;

        Info<< "Time = " << runTime.timeName() << nl << endl;

        // --- PIMPLE loop
        while (pimple.loop())
        {
#ifdef  OPENFOAMFOUNDATION
            if (pimple.firstPimpleIter() || moveMeshOuterCorrectors)
#endif
#ifdef  OPENFOAMESI
            if (pimple.firstIter() || moveMeshOuterCorrectors)
#endif
#ifdef OPENFOAMESIORFOUNDATION
            {
                mesh.update();

                if (mesh.changing())
                {
                    MRF.update();

                    if (correctPhi)
                    {
                        // Calculate absolute flux
                        // from the mapped surface velocity
                        phi = mesh.Sf() & Uf();

                        #include "correctPhi.H"

                        // Make the flux relative to the mesh motion
                        fvc::makeRelative(phi, U);
                    }

                    if (checkMeshCourantNo)
                    {
                        #include "meshCourantNo.H"
                    }
                }
            }
#endif
            
#           include "UEqn.H"

            // --- PISO loop
            while (pimple.correct())
            {
#               include "pEqn.H"
            }

            // Calculate H equation

#			include "HEqn.H"

            
            if (haemoSwitch.value() == 0 || runTime < haemoSwitchTime)
            {
                Info<< "Not Solving for H, Migration Model is inactive 1" << nl << endl;
            } else
            {
#ifdef OPENFOAMFOUNDATION
                if (!pimple.finalPimpleIter())
#else
                if (!pimple.finalIter())
#endif
                {
                    HEqn.relax();                   // use these two lines
                    HEqn.solve().initialResidual(); // for underrelaxed solver
                } else
                {
                    HEqn.solve(); // or this one for no underrelaxation (unstable for SIMPLE)
                    Info<< "Last PIMPLE iteration, no underrelaxation!" << nl << endl;
                }
            }

            // Back to original code

            if (pimple.turbCorr())
                {
                    laminarTransport.correct();
                    turbulence->correct();
                }
                
//#ifdef OPENFOAMESIORFOUNDATION
//            }
//#endif

        }

#ifdef OPENFOAMESIORFOUNDATION
    /* Updating the Windkessel struct data structure*/
    if (WK_FLAG > 0)
    {
      Info << nl << "Updating Windkessel values ..." << endl;
  		execute_at_end(mesh, phi, WKpressures, p, windkesselProperties, runTime.value());
    }
#endif

        runTime.write();

        Info<< "ExecutionTime = " << runTime.elapsedCpuTime() << " s"
            << "  ClockTime = " << runTime.elapsedClockTime() << " s"
            << nl << endl;
    }

    Info<< "End\n" << endl;

    return 0;
}


// ************************************************************************* //
