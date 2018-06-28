// Fill out your copyright notice in the Description page of Project Settings.

#include "UVCamera.h"
#include "ConstructorHelpers.h"
#include "Engine.h"
#include <algorithm>
#include <sstream>
#include <chrono>

// Sets default values
AUVCamera::AUVCamera()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	Width = 480;
	Height = 300;
	FieldOfView = 90.0;
	FrameRate = 0;
	FDateTime now = FDateTime::UtcNow();

	bImageSameSize = false;
	bCaptureColorImage = false;
	bInitialAsyncTask = true;
	bColorFirsttick = true;
	bColorSave = false;

	ColorImgCaptureComp = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("ColorCapture"));
	ColorImgCaptureComp->SetupAttachment(RootComponent);
	ColorImgCaptureComp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	ColorImgCaptureComp->TextureTarget = CreateDefaultSubobject<UTextureRenderTarget2D>(TEXT("ColorTarget"));
	ColorImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);
	ColorImgCaptureComp->FOVAngle = FieldOfView;

	ColorImgCaptureComp->SetHiddenInGame(true);
	ColorImgCaptureComp->Deactivate();

}

// Called when the game starts or when spawned
void AUVCamera::BeginPlay()
{
	Super::BeginPlay();
	FTimerHandle TimerHandle;
	GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &AUVCamera::Initial, 1.0f, false);

}

// Called every frame
void AUVCamera::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	FVector Position = GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetCameraLocation();
	FRotator Rotation = GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetCameraRotation();
	ColorImgCaptureComp->SetWorldLocationAndRotation(Position, Rotation);

}

void AUVCamera::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	//StopAsyncTask(ColorAsyncWorker);
}

void AUVCamera::Initial()
{
	if (bSaveAsImage)
	{
		static IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
	}

	if (bImageSameSize)
	{
		ColorViewport = GetWorld()->GetGameViewport()->Viewport;
		Width = ColorViewport->GetRenderTargetTextureSizeXY().X;
		Height = ColorViewport->GetRenderTargetTextureSizeXY().Y;
		ColorImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);

	}
	ColorImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);
	// Initializing buffers for reading images from the GPU
	ColorImage.AddZeroed(Width*Height);
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("Image Size: x: %i, y: %i"), Width, Height));

	if (bCaptureColorImage) {

		ColorImgCaptureComp->TextureTarget->TargetGamma = 1.4;
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, FString::Printf(TEXT("Gamma:%f"), GEngine->GetDisplayGamma()));
		ColorImgCaptureComp->SetHiddenInGame(false);
		ColorImgCaptureComp->Activate();
	}
	// Call the timer 
	SetFramerate(FrameRate);
}

void AUVCamera::SetFramerate(const float NewFramerate)
{
	if (NewFramerate > 0.0f)
	{
		// Update Camera on custom timer tick (does not guarantees the UpdateRate value,
		// since it will be eventually triggered from the game thread tick

		FTimerHandle TimerHandle;
		GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &AUVCamera::TimerTick, 1 / NewFramerate, true);

	}
}

void AUVCamera::InitAsyncTask(FAsyncTask<RawDataAsyncWorker>* AsyncWorker, TArray<FColor>& image, FDateTime Stamp, FString Name, int Width, int Height)
{

	AsyncWorker = new FAsyncTask<RawDataAsyncWorker>(image, ImageWrapper, Stamp, Name, Width, Height);
	AsyncWorker->StartBackgroundTask();
	AsyncWorker->EnsureCompletion();


}

void AUVCamera::CurrentAsyncTask(FAsyncTask<RawDataAsyncWorker>* AsyncWorker)
{
	if (AsyncWorker->IsDone() || bInitialAsyncTask)
	{
		bInitialAsyncTask = false;
		UE_LOG(LogTemp, Warning, TEXT("Start writing Color Image"));
		AsyncWorker->StartBackgroundTask();
		AsyncWorker->EnsureCompletion();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[%s][%d] SKIP new task "), TEXT(__FUNCTION__), __LINE__);
	}

}

void AUVCamera::StopAsyncTask(FAsyncTask<RawDataAsyncWorker>* AsyncWorker)
{
	if (AsyncWorker)
	{
		// Wait for worker to complete before deleting it
		AsyncWorker->EnsureCompletion();
		delete AsyncWorker;
		AsyncWorker = nullptr;
	}
}

void AUVCamera::TimerTick()
{
	FDateTime Stamp = FDateTime::UtcNow();

	if (bCaptureColorImage)
	{
		if (!bColorFirsttick && ColorPixelFence.IsFenceComplete()) {
			if (bSaveAsImage) {

				InitAsyncTask(ColorAsyncWorker, ColorImage, Stamp, TEXT("COLOR"), Width, Height);
				bColorSave = true;
			}
			StopAsyncTask(ColorAsyncWorker);
		}
		if (bColorFirsttick)
		{
			ProcessColorImg();
			bColorFirsttick = false;
		}
		else if (bColorSave)
		{
			ProcessColorImg();
			bColorSave = false;
		}
		UE_LOG(LogTemp, Warning, TEXT("Read Color Image"));
	}
}

void AUVCamera::ProcessColorImg()
{
	FReadSurfaceDataFlags ReadSurfaceDataFlags;
	ReadSurfaceDataFlags.SetLinearToGamma(false);
	FTextureRenderTargetResource* ColorRenderResource = ColorImgCaptureComp->TextureTarget->GameThread_GetRenderTargetResource();
	if (bCaptureViewport) {
		ColorViewport->Draw();
		ColorViewport->ReadPixels(ColorImage);
	}
	if (bCaptureScencComponent)
	{
		ReadPixels(ColorRenderResource, ColorImage);
	}

	ColorPixelFence.BeginFence();
}



void AUVCamera::ReadPixels(FTextureRenderTargetResource *& RenderResource, TArray<FColor>& OutImageData, FReadSurfaceDataFlags InFlags, FIntRect InRect)
{

	// Read the render target surface data back.	
	if (InRect == FIntRect(0, 0, 0, 0))
	{
		InRect = FIntRect(0, 0, RenderResource->GetSizeXY().X, RenderResource->GetSizeXY().Y);
	}
	struct FReadSurfaceContext
	{
		FRenderTarget* SrcRenderTarget;
		TArray<FColor>* OutData;
		FIntRect Rect;
		FReadSurfaceDataFlags Flags;
	};

	OutImageData.Reset();
	FReadSurfaceContext ReadSurfaceContext =
	{
		RenderResource,
		&OutImageData,
		InRect,
		InFlags,
	};

	ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(
		ReadSurfaceCommand,
		FReadSurfaceContext, Context, ReadSurfaceContext,
		{
			RHICmdList.ReadSurfaceData(
				Context.SrcRenderTarget->GetRenderTargetTexture(),
				Context.Rect,
				*Context.OutData,
				Context.Flags
			);
		});
}