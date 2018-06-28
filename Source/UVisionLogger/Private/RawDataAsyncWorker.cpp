// Fill out your copyright notice in the Description page of Project Settings.

#include "RawDataAsyncWorker.h"
#include "Runtime/Core/Public/Misc/FileHelper.h"
#include "Runtime/Core/Public/GenericPlatform/GenericPlatformFile.h"
#include "Runtime/Core/Public/HAL/PlatformFilemanager.h"

RawDataAsyncWorker::RawDataAsyncWorker(TArray<FColor>& Image_init, TSharedPtr<IImageWrapper>& ImageWrapperRef, FDateTime Stamp, FString Name, int Width_init, int Height_init)
{
	Width = Width_init;
	Height = Height_init;
	TimeStamp = Stamp;
	ImageName = Name;
	ImageWrapper = ImageWrapperRef;
	Image = Image_init;

}

RawDataAsyncWorker::~RawDataAsyncWorker()
{
	UE_LOG(LogTemp, Warning, TEXT("Task Deleted"));
}

TStatId RawDataAsyncWorker::GetStatId() const
{
	return TStatId();
}

void RawDataAsyncWorker::DoWork()
{
	UE_LOG(LogTemp, Warning, TEXT("Task Begin"));
	if (Width > 0 && Height > 0)
	{
		SaveImage(Image, ImageWrapper, TimeStamp, ImageName, Width, Height);
	}

}

void RawDataAsyncWorker::SetLogToImage()
{
}

void RawDataAsyncWorker::SaveImage(TArray<FColor>& image, TSharedPtr<IImageWrapper>& ImageWrapper, FDateTime Stamp, FString ImageName, int Width, int Height)
{
	// get the time stamp
	FString TimeStamp = FString::FromInt(Stamp.GetYear()) + "_" + FString::FromInt(Stamp.GetMonth()) + "_" + FString::FromInt(Stamp.GetDay())
		+ "_" + FString::FromInt(Stamp.GetHour()) + "_" + FString::FromInt(Stamp.GetMinute()) + "_" + FString::FromInt(Stamp.GetSecond()) + "_" +
		FString::FromInt(Stamp.GetMillisecond());
	UE_LOG(LogTemp, Warning, TEXT("Height %i,Width %i"), Height, Width);
	// initial Image Wrapper
	TArray<uint8> ImgData;
	ImageWrapper->SetRaw(image.GetData(), image.GetAllocatedSize(), Width, Height, ERGBFormat::BGRA, 8);
	ImgData = ImageWrapper->GetCompressed();



	//save image in local disk as image
	FString FileName = ImageName + TimeStamp + ".jpg";
	FString FileDir = FPaths::ProjectSavedDir() + "/" + "viewport";
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*FileDir)) {
		PlatformFile.CreateDirectoryTree(*FileDir);

	}
	FString AbsolutePath = FileDir + "/" + FileName;
	FFileHelper::SaveArrayToFile(ImgData, *AbsolutePath);
}

