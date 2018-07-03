// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "Engine.h"
#include "RawDataAsyncWorker.h"
#include "Runtime/Core/Public/Async/AsyncWork.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "UVCamera.generated.h"

/**
 * 
 */
UCLASS()
class UVISIONLOGGER_API AUVCamera : public ACameraActor
{
	GENERATED_BODY()
public:
	AUVCamera();
	//Set UVCamera id
	UPROPERTY(EditAnyWhere, Category = "Vision Settings")
		FString CameraId;
	//Set UVCamera follow first player view
	UPROPERTY(EditAnyWhere, Category = "Vision Settings")
		bool bFirstPersonView;

	// Set Caputure Image Size the same as Viewport Size
	UPROPERTY(EditAnywhere, Category = "Vision Settings")
		bool bImageSameSize;

	// Camera Width
	UPROPERTY(EditAnywhere, Category = "Vision Settings")
		uint32 Width;

	// Camera Height
	UPROPERTY(EditAnywhere, Category = "Vision Settings")
		uint32 Height;

	// Camera field of view
	UPROPERTY(EditAnywhere, Category = "Vision Settings")
		float FieldOfView;

	// Camera update rate
	UPROPERTY(EditAnywhere, Category = "Vision Settings")
		float FrameRate;
	// Capture Color image
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode")
		bool bCaptureColorImage;

	// CaptureViewport
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode|Color Mode", meta = (EditCondition = bCaptureColorImage))
		bool bCaptureViewport;
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode|Color Mode", meta = (EditCondition = bCaptureColorImage))
		bool bCaptureScencComponent;

	// Save data as image
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode|Save Mode")
		bool bSaveAsImage;

	// Intial Asynctask
	bool bInitialAsyncTask;



protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called when the game ends
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// initialize Componets
	void Initial();

	// Change the framerate on the fly
	void SetFramerate(const float NewFramerate);

	// Initate AsyncTask
	void InitAsyncTask(FAsyncTask<RawDataAsyncWorker>* AsyncWorker, TArray<FColor>& image, FDateTime Stamp, FString Name, int Width, int Height);

	// Start AsyncTask
	void CurrentAsyncTask(FAsyncTask<RawDataAsyncWorker>* AsyncWorker);

	// Stop AsyncTask
	void StopAsyncTask(FAsyncTask<RawDataAsyncWorker>* AsyncWorker);

private:
	// Camera capture component for color images (RGB)
	FViewport* ColorViewport;
	USceneCaptureComponent2D* ColorImgCaptureComp;

	// Color image buffer
	TArray<FColor> ColorImage;

	// Image Wrapper for saving image
	TSharedPtr<IImageWrapper> ImageWrapper;

	FRenderCommandFence ColorPixelFence;
	
	// helper bool for Reading and Saving 
	bool bColorFirsttick;
	bool bColorSave;

	// Timer callback (timer tick)
	void TimerTick();

	// Read Color raw data
	void ProcessColorImg();

	// Read Raw data from USceneCaptureComponent2D
	void ReadPixels(FTextureRenderTargetResource*& RenderResource, TArray< FColor >& OutImageData, FReadSurfaceDataFlags InFlags = FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX), FIntRect InRect = FIntRect(0, 0, 0, 0));

	//Async worker to save the color raw data
	FAsyncTask<RawDataAsyncWorker>* ColorAsyncWorker;


	
};
