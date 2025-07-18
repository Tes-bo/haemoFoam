/*--------------------------------*- C++ -*----------------------------------*\
| =========                 |                                                 |
| \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox           |
|  \\    /   O peration     | Version:  v1812                                 |
|   \\  /    A nd           | Web:      www.OpenFOAM.com                      |
|    \\/     M anipulation  |                                                 |
\*---------------------------------------------------------------------------*/


/*---------------------------------------------------------------------------*\
Application
    haemoPostProcess

Description
    Calculates and writes WSS derived metrics for haemodynamic cases


    WSS: WSS, gradient of U at the wall, multiplied with local
    * kinematic viscosity * density

    avgWSS: average WSS for the time range

    OSI: Oscillatory Shear Stress Index

    transWSS: Transverse WSS

    All WSS calculations are using kinematic values in
    * accordance to FOAMs stress and pressure definitions and are
    * multiplied with the density rho as defined in transportProperties
    * to get the commonly used dynamic values.

    Be aware that pressures in OpenFoam are kinematic pressures!

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
#include "turbulenceModel.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

int main(int argc, char *argv[])
{
    timeSelector::addOptions();
#   include "setRootCase.H"
#   include "createTime.H"
    instantList timeDirs = timeSelector::select0(runTime, args);
#   include "createMesh.H"

// #   include "readHaemoProperties.H"

    IOdictionary transportProperties
        (
         IOobject
         (
          "transportProperties", // name of the dictionary
          runTime.constant(), // location in the case - this one is in constant
          mesh, // needs the mesh object reference to do some voodoo - unimportant now
          IOobject::MUST_READ,
          IOobject::NO_WRITE // read-only
         )
        );

    // const dimensionedScalar rho(transportProperties.lookup("rho"));
    dimensionedScalar rho
    (
        // "rho",
        // viscosityProperties.lookup("rho")
        "rho",
        transportProperties.lookup("rho")
    );

    // Info << rho << endl;


    volVectorField normalVector
        (
         IOobject
         (
          "normalVector",
          runTime.timeName(),
          mesh,
          IOobject::NO_READ,
          IOobject::AUTO_WRITE
         ),
         mesh,
         dimensionedVector
         (
          "normalVector",
          dimLength,
          vector::zero
         )
        );

    volVectorField TAWSS
        (
         IOobject
         (
          "TAWSS",
          runTime.timeName(),
          mesh,
          IOobject::NO_READ,
          IOobject::AUTO_WRITE
         ),
         mesh,
         dimensionedVector
         (
          "TAWSS",
          dimMass/(dimLength*sqr(dimTime)),
          //sqr(dimLength)/sqr(dimTime),
          vector::zero
         )
        );

    volScalarField transWSS
        (
         IOobject
         (
          "transWSS",
          runTime.timeName(),
          mesh,
          IOobject::NO_READ,
          IOobject::AUTO_WRITE
         ),
         mesh,
         dimensionedScalar
         (
          "transWSS",
          dimMass/(dimLength*sqr(dimTime)),
          //sqr(dimLength)/sqr(dimTime),
          0
         )
        );

    volScalarField TAWSSMag
        (
         IOobject
         (
          "TAWSSMag",
          runTime.timeName(),
          mesh,
          IOobject::NO_READ,
          IOobject::AUTO_WRITE
         ),
         mesh,
         dimensionedScalar
         (
          "TAWSSMag",
          dimMass/(dimLength*sqr(dimTime)),
          //sqr(dimLength)/sqr(dimTime),
          0
         )
        );


    volScalarField divTAWSS
        (
        IOobject
        (
        "divTAWSS",
        runTime.timeName(),
        mesh,
        IOobject::NO_READ,
        IOobject::AUTO_WRITE
        ),
        mesh,
        dimensionedScalar
        (
        "divTAWSS",
        dimless/dimLength,
        // dimMass/(dimLength * dimLength * sqr(dimTime)),
        //sqr(dimLength)/sqr(dimTime),
        0
        )
        );

    volScalarField OSI
        (
         IOobject
         (
          "OSI",
          runTime.timeName(),
          mesh,
          IOobject::NO_READ,
          IOobject::AUTO_WRITE
         ),
         mesh,
         dimensionedScalar
         (
          "OSI",
          dimless,
          0
         )
        );

    volScalarField RRT
        (
         IOobject
         (
          "RRT",
          runTime.timeName(),
          mesh,
          IOobject::NO_READ,
          IOobject::AUTO_WRITE
         ),
         mesh,
         dimensionedScalar
         (
          "RRT",
          (dimLength*sqr(dimTime))/dimMass,
          //sqr(dimTime)/sqr(dimLength),
          0
         )
        );

    volScalarField TAHct
        (
         IOobject
         (
          "TAHct",
          runTime.timeName(),
          mesh,
          IOobject::NO_READ,
          IOobject::AUTO_WRITE
         ),
         mesh,
         dimensionedScalar
         (
          "TAHct",
          dimless,
          0
         )
        );

    volScalarField H
        (
         IOobject
         (
          "H",
          runTime.timeName(),
          mesh,
          IOobject::READ_IF_PRESENT,
          IOobject::AUTO_WRITE
         ),
         mesh,
         dimensionedScalar
         (
          "H",
          dimless,
          0
         )
        );



    int nfield = 0;

    // Define a flag if H exists or not. If not, we don't calculate THct.
    // This way we can use the haemoPostProcess code for regular pimpleFoam
    // results.
    //
    bool Hexists = true;

    // First run, calculate WSS, avgWSS and OSI

    Info<< "First Run - calculating WSS, average WSS, OSI, RRT" << endl;

    forAll(timeDirs, timeI)
    {
        runTime.setTime(timeDirs[timeI], timeI);
        Info<< "Time = " << runTime.timeName() << "    " << endl;

        IOobject Uheader
            (
             "U",
             runTime.timeName(),
             mesh,
             IOobject::MUST_READ
            );

        IOobject wallShearStressheader
            (
             "wallShearStress",
             runTime.timeName(),
             mesh,
             IOobject::MUST_READ
            );
        
        IOobject Hheader
           (
            "H",
            runTime.timeName(),
            mesh,
            IOobject::MUST_READ
           );


        // Check  wallShearStress exists
            // if (Hheader.headerOk())
            // {
                mesh.readUpdate();

                Info<< "    Reading wallShearStress" << endl;
                volVectorField wallShearStress(wallShearStressheader, mesh);

                if (Hheader.typeHeaderOk<volScalarField>(true))
                {
                    Info<< "    Reading H" << endl;
                    volScalarField H(Hheader, mesh);
                } else {
                    Info << "    H field not found, assuming case was not run with haemoFoam" << endl;
                    Hexists = false;
                }

                Info<< "    Calculating WSS" << endl;

                volVectorField WSS
                    (
                     IOobject
                     (
                      "WSS",
                      runTime.timeName(),
                      mesh,
                      IOobject::NO_READ,
                      IOobject::AUTO_WRITE
                     ),
                     mesh,
                     dimensionedVector
                     (
                      "WSS",
                      dimMass/(dimLength*sqr(dimTime)),
                      vector::zero
                     )
                    );

                volScalarField WSSMag
                    (
                     IOobject
                     (
                      "WSSMag",
                      runTime.timeName(),
                      mesh,
                      IOobject::NO_READ,
                      IOobject::AUTO_WRITE
                     ),
                     mesh,
                     dimensionedScalar
                     (
                      "WSSMag",
                      dimMass/(dimLength*sqr(dimTime)),
                      0
                     )
                    );

                volScalarField divU
                    (
                    IOobject
                    (
                    "divU",
                    runTime.timeName(),
                    mesh,
                    IOobject::NO_READ,
                    IOobject::AUTO_WRITE
                    ),
                    mesh,
                    dimensionedScalar
                    (
                    "divU",
                    dimLength/(dimLength * dimLength),
                    0
                    )
                    );

                volScalarField divWSS
                    (
                    IOobject
                    (
                    "divWSS",
                    runTime.timeName(),
                    mesh,
                    IOobject::NO_READ,
                    IOobject::AUTO_WRITE
                    ),
                    mesh,
                    dimensionedScalar
                    (
                    "divWSS",
                    dimless/dimLength,
                    // dimMass/(dimLength * dimLength * sqr(dimTime)),
                    //sqr(dimLength)/sqr(dimTime),
                    0
                    )
                    );

                    WSS =
                         wallShearStress
                         * rho;

                    WSSMag =
                        mag(wallShearStress)
                        * rho;

                    dimensionedScalar epsWSS = dimensionedScalar("epsWSS",WSS.dimensions(), 1e-64);

                    divWSS =
                         fvc::div(WSS/(mag(WSS)+epsWSS));

                    divTAWSS += divWSS;

                    TAWSS +=
                         wallShearStress
                         * rho;

                    TAWSSMag +=
                        mag(-wallShearStress)
                        * rho;

                    if (Hexists) {
                        TAHct += H ;
                    } else {
                        Info << "H field not found" << endl;
                    }


                // forAll(WSS.boundaryField(), patchi)
                // {

                //     WSS.boundaryFieldRef()[patchi] =
                //          wallShearStress.boundaryField()[patchi]
                //          * rho.value();

                //     WSSMag.boundaryFieldRef()[patchi] =
                //         mag(wallShearStress.boundaryField()[patchi])
                //         * rho.value();

                //     // divWSS.boundaryFieldRef()[patchi] =
                //     //     fvc::div(wallShearStress);
                //          // * rho.value();


                //     TAWSS.boundaryFieldRef()[patchi] +=
                //          wallShearStress.boundaryField()[patchi]
                //          * rho.value();

                //     TAWSSMag.boundaryFieldRef()[patchi] +=
                //         mag(-wallShearStress.boundaryField()[patchi])
                //         * rho.value() ;

                //     TAHct.boundaryFieldRef()[patchi] +=
                //         H.boundaryField()[patchi] ;
                // }
                
                WSS.write();
                WSSMag.write();
                divWSS.write();

                nfield++;
            }
            // else
            // {
            //     Info<< "    No wallShearStress, run the command" << endl;
            //     Info<< "    haemoPimpleFoam -postProcess -func wallShearStress" << endl;
            //     Info<< "    before running haemoPostProcess" << endl;
            // }

        // Check U_0 exists for TWSSG
        // if (U_0header.headerOk())
        // {
        //     if (nuheader.headerOk())
        //     {
                mesh.readUpdate();
    // }
    
    Info<< "Writing TAWSS, TAWSSMag, divWSS, OSI, RRT, normalVectors" << endl;
    
    // devide by the number of added fields
    if(nfield>0){
        Info<< "number of fields added: "<< nfield << endl;

        if (Hexists) {
            TAHct /= nfield;
        }

        TAWSS /= nfield;

        TAWSSMag /= nfield;

        dimensionedScalar epsWSS = dimensionedScalar("epsWSS",TAWSS.dimensions(), 1e-64);

        divTAWSS = fvc::div(TAWSS/(mag(TAWSS)+epsWSS));

        OSI =
            0.5
            * ( 1 - mag(TAWSS)
            /(TAWSSMag+epsWSS)
            );
        Info<< "OSI" << endl;

        RRT =
            1
            /((
                (1 - 2.0*OSI)
                *
                TAWSSMag
            )+epsWSS);
        Info<< "RRT" << endl;

        forAll(TAWSS.boundaryField(), patchi)
        {
            // TAWSS.boundaryFieldRef()[patchi] /= nfield;

            // TAWSSMag.boundaryFieldRef()[patchi] /= nfield;


            // OSI.boundaryFieldRef()[patchi] =
            //     0.5
            //     * ( 1 - mag(TAWSS.boundaryField()[patchi])
            //             /(TAWSSMag.boundaryField()[patchi]+1e-64)
            //       );

            // RRT.boundaryFieldRef()[patchi] =
            //     1
            //     /((
            //                 (1 - 2.0*OSI.boundaryField()[patchi])
            //                 *
            //                 TAWSSMag.boundaryField()[patchi]
            //       )+1e-64);

            normalVector.boundaryFieldRef()[patchi] =
                mesh.Sf().boundaryField()[patchi]
                /mesh.magSf().boundaryField()[patchi]
                ;
        }
    }
    
    
    
    // Second run, calculate transWSS
    
    Info<< "Second Run - calculating transWSS" << endl;
    
    nfield = 0;
    
    forAll(timeDirs, timeI)
    {
        runTime.setTime(timeDirs[timeI], timeI);
        Info<< "Time = " << runTime.timeName() << "    " << endl;
    
        IOobject WSSheader
            (
             "WSS",
             runTime.timeName(),
             mesh,
             IOobject::MUST_READ
            );
    
        // Check WSS exist
        // if (WSSheader.headerOk())
        // {
            mesh.readUpdate();
    
            Info<< "    Reading WSS" << endl;
            volVectorField WSS(WSSheader, mesh);
    
            forAll(transWSS.boundaryField(), patchi)
            {
                transWSS.boundaryFieldRef()[patchi] +=
                    mag(
                            WSS.boundaryField()[patchi]
                            &
                            (
                             normalVector.boundaryField()[patchi]
                             ^
                             (
                              TAWSS.boundaryField()[patchi]
                              / (mag(TAWSS.boundaryField()[patchi])+1e-64)
                             )
                            )
                       );
            }

            nfield++;
        }
        // else
        // {
        //     Info<< "    No WSS" << endl;
        // }
    
    
    Info<< "Writing transWSS" << endl;
    
    // devide by the number of added fields
    if(nfield>0){
        Info<< "number of fields added: "<< nfield << endl;
    
        forAll(transWSS.boundaryField(), patchi)
        {
            transWSS.boundaryFieldRef()[patchi] /= nfield;
        }
    }

    Info << "writing WSS metrics to all processed time directories" << endl;
    forAll(timeDirs, timeI)
    {
    runTime.setTime(timeDirs[timeI], timeI);
    mesh.readUpdate();
    transWSS.write();
    TAWSS.write();
    TAWSSMag.write();
    divTAWSS.write();
    OSI.write();
    RRT.write();
    normalVector.write();
    if (Hexists){
        TAHct.write();
    }
    }

    Info<< "End" << endl;
    return 0;
}



// ************************************************************************* //
